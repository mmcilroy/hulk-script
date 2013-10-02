
extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "hulk/core/tcp.h"
#include "hulk/core/thread.h"
#include "hulk/core/logger.h"
#include "hulk/fix/tcp.h"

#include <list>
#include <ctime>

using namespace hulk;

const char* INITIATOR_ID = "luaL_initiator";

log& lg = logger::instance().get( "hulk.script" );

fix::tcp_event_loop io_loop;

class io_thread : public thread
{
    virtual void run() {
        while( 1 ) io_loop.loop( 1000 );
    }
};

class scriptable_initiator : public fix::session
{
public:
    scriptable_initiator( const fix::value& protocol, const fix::fields& header, fix::transport& tpt )
    : session( protocol, header, tpt ) {}

    virtual void recv( const fix::fields& msg, const std::string buf )
    {
        lock_guard guard( _mutex );
        message_data data;
        data._fields = msg;
        data._buf = buf;
        _messages.push_back( data );
    }

    bool recvd( fix::fields& msg )
    {   
        lock_guard guard( _mutex );
        if( _messages.size() )
        {
            message_data& md = *_messages.begin();

            std::stringstream ss;
            for( int i=0; i< md._fields.size(); i++ ) {
                ss << "\n    " << md._fields[i]._tag << " = " << md._fields[i]._value;
            }

            LOG_INFO( lg, "recvd " << md._fields.size() << " fields" << ss.str() );

            msg = md._fields;
            _messages.pop_front();

            return true;
        }

        return false;
    }

private:
    struct message_data
    {
        fix::fields _fields;
        std::string _buf;
    };

    mutex _mutex;
    std::list< message_data > _messages;
};

struct decoder_cb
{
    fix::fields _msg;
    void operator()( const fix::fields& msg, const std::string buf ) {
        _msg = msg;
    }
};

void push_fix( lua_State* l, fix::fields& flds )
{
    lua_newtable( l );
    for( int i=0; i<flds.size(); i++ )
    {
        std::string& str = flds[i]._value;
        lua_pushinteger( l, flds[i]._tag );
        lua_pushlstring( l, str.c_str(), str.size() );
        lua_settable( l, -3 );
    }
}

void push_fix( lua_State* l, const std::string& flds )
{
    decoder_cb cb;
    fix::decoder d;
    d.decode( cb, flds.c_str(), flds.size() );
    push_fix( l, cb._msg );
}

void pop_fix( lua_State* l, fix::fields& flds )
{
    lua_pushnil( l );
    while( lua_next( l, -2 ) != 0 )
    {
        flds.push_back( fix::field( lua_tointeger( l, -2 ), lua_tostring( l, -1 ) ) );
        lua_pop( l, 1 );
    }
}

void fatal_error( lua_State* l, const std::string& err )
{
    lua_Debug ar;
    lua_getstack( l, 1, &ar );
    lua_getinfo( l, "Slnt", &ar );

    LOG_ERROR( lg, err << " at " << ar.short_src << ":" << ar.currentline );
    exit( 1 );
}

scriptable_initiator* l_check_initiator( lua_State* l, int n )
{
    return *(scriptable_initiator**)luaL_checkudata( l, n, INITIATOR_ID );
}

int l_new_initiator( lua_State* l )
{
    const char* transport_uri = luaL_checkstring( l, -3 );
    const char* protocol = luaL_checkstring( l, -2 );
    fix::fields header; pop_fix( l, header );

    std::string uri( &transport_uri[6] );
    size_t s = uri.find( ':' );
    std::string host = uri.substr( 0, s );
    std::string port = uri.substr( s+1 );

    LOG_INFO( lg, "connecting to " << host << ":" << port );

    scriptable_initiator** udata = (scriptable_initiator**)lua_newuserdata( l, sizeof( scriptable_initiator* ) );
    *udata = io_loop.new_initiator< scriptable_initiator >( host.c_str(), atoi( port.c_str() ), protocol, header );
    luaL_getmetatable( l, INITIATOR_ID );
    lua_setmetatable( l, -2 );

    return 1;
}

int l_del_initiator( lua_State* l )
{
    scriptable_initiator* si = l_check_initiator( l, -3 );
    delete si;

    return 0;
}

int l_send( lua_State* l )
{
    scriptable_initiator* si = l_check_initiator( l, -3 );
    const char* msg_type = luaL_checkstring( l, -2 );
    fix::fields body; pop_fix( l, body );

    std::string s;
    si->send( msg_type, body, &s );
    push_fix( l, s );

    return 1;
}

int l_recv( lua_State* l )
{
    scriptable_initiator* si = l_check_initiator( l, -1 );
    fix::fields msg;

    LOG_INFO( lg, "recv..." );

    int i=0;
    while( !si->recvd( msg ) && i < 200 ) {
        sleep_ms( 50 ); ++i;
    }

    if( i < 200 ) {
        push_fix( l, msg );
    } else {
        lua_pushnil( l );
    }

    return 1;
}

int l_expect( lua_State* l )
{
    scriptable_initiator* si = l_check_initiator( l, -2 );
    fix::fields check_flds;
    pop_fix( l, check_flds );

    LOG_INFO( lg, "expecting " << check_flds.size() << " fields..." );

    int i=0;
    fix::fields recvd_flds;
    while( !si->recvd( recvd_flds ) && i < 200 ) {
        sleep_ms( 50 ); ++i;
    }

    if( i < 200 )
    {
        fix::field_map recvd_map( recvd_flds );

        for( int i=0; i<check_flds.size(); i++ )
        {
            fix::tag tag = check_flds[i]._tag;
            fix::value& val = check_flds[i]._value;

            LOG_INFO( lg, "expect: " << tag << "=" << val );

            if( recvd_map.count( tag ) )
            {
                const fix::value* fld = recvd_map[tag];
                if( val != *fld )
                {
                    std::stringstream ss;
                    ss << "value mismatch: " << val << "!=" << *fld;
                    fatal_error( l, ss.str() );
                }
            }
            else
            {
                std::stringstream ss;
                ss << "no such field - " << tag;
                fatal_error( l, ss.str() );
            }

            lua_pop( l, 1 );

            push_fix( l, recvd_flds );
        }
    }
    else
    {
        fatal_error( l, "timed out" );
    }

    return 1;
}

int l_uuid( lua_State* l )
{
    static size_t suffix = 0;
    static time_t prefix;
    time( &prefix );

    char id[16];
    sprintf( id, "%05d-%08d", (int)prefix%86400, (int)suffix++ );
    lua_pushstring( l, id );

    return 1;
}

int l_sleep( lua_State* L )
{
    if( lua_gettop( L ) == 1 )
    {
        luaL_checktype( L, -1, LUA_TNUMBER );
        sleep_ms( 1000 * lua_tointeger( L, -1 ) );
    }

    return 0;
}

int l_script_error( lua_State* l )
{
    const char* errmsg = luaL_checkstring( l, -1 );

    lua_Debug ar;
    lua_getstack( l, 1, &ar );
    lua_getinfo( l, "Slnt", &ar );

    std::cerr << std::endl << errmsg << " at " << ar.short_src << ":" << ar.currentline << std::endl;
    std::cerr << "exiting...\n\n";
    exit( 1 );

    return 0;
}

void l_register( lua_State* l )
{
    luaL_Reg reg_fix[] =
    {
        { "new_initiator", l_new_initiator },
        { "__gc", l_del_initiator },
        { "send", l_send },
        { "recv", l_recv },
        { "expect", l_expect },
        { "uuid", l_uuid },
        { "sleep", l_sleep },
        { NULL, NULL }
    };

    luaL_newmetatable( l, INITIATOR_ID );
    luaL_setfuncs( l, reg_fix, 0 );
    lua_pushvalue( l, -1 );
    lua_setfield( l, -1, "__index" );
    lua_setglobal( l, "fix" );

    luaL_Reg reg_script[] =
    {
        { "error", l_script_error },
        { NULL, NULL }
    };

    lua_newtable( l ); 
    lua_setglobal( l, "script" ); 
    lua_getglobal( l, "script" ); 
    luaL_setfuncs( l, reg_script, 0 );
}

int main( int argc, char** argv )
{
    io_thread io;

    lua_State* l = luaL_newstate();
    luaL_openlibs( l );
    l_register( l );

    LOG_INFO( lg, "running script: " << argv[1] );

    int err = luaL_dofile( l, argv[1] );
    if( err ) {
        LOG_ERROR( lg, "lua error: " << luaL_checkstring( l, -1 ) );
    }

    lua_close( l );

    io.join();
}

