
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

core::log& log = core::logger::instance().get( "hulk.script" );

fix::tcp_event_loop io_loop;

void fatal_error( const std::string& err )
{
    LOG_ERROR( log, err );
    exit( 1 );
}

void print_fix( const fix::fields& f )
{
    for( int i=0; i<f.size(); i++ ) {
        std::cout << f[i]._tag << " = " << f[i]._value << std::endl;
    }
    std::cout << std::endl;
}

class io_thread : public core::thread
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
        core::lock_guard guard( _mutex );
        message_data data;
        data._fields = msg;
        data._buf = buf;
        _messages.push_back( data );
    }

    bool recvd( fix::fields& msg )
    {
        core::lock_guard guard( _mutex );
        if( _messages.size() )
        {
            LOG_INFO( log, "recvd - " << (*_messages.begin())._buf );
            msg = (*_messages.begin())._fields;
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

    core::mutex _mutex;
    std::list< message_data > _messages;
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

void pop_fix( lua_State* l, fix::fields& flds )
{
    lua_pushnil( l );
    while( lua_next( l, -2 ) != 0 )
    {
        flds.push_back( fix::field( lua_tointeger( l, -2 ), lua_tostring( l, -1 ) ) );
        lua_pop( l, 1 );
    }
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

    LOG_INFO( log, "connecting to " << host << ":" << port );

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
    si->send( msg_type, body );

    return 0;
}

int l_recv( lua_State* l )
{
    scriptable_initiator* si = l_check_initiator( l, -1 );
    fix::fields msg;

    int i=0;
    while( !si->recvd( msg ) && i < 100 ) {
        core::sleep_ms( 50 ); ++i;
    }

    if( i < 100 ) {
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

    int i=0;
    fix::fields recvd_flds;
    while( !si->recvd( recvd_flds ) && i < 100 ) {
        core::sleep_ms( 50 ); ++i;
    }

    if( i < 100 )
    {
        fix::field_map recvd_map( recvd_flds );

        for( int i=0; i<check_flds.size(); i++ )
        {
            fix::tag tag = check_flds[i]._tag;
            fix::value& val = check_flds[i]._value;

            LOG_INFO( log, "expect: " << tag << "=" << val );

            if( recvd_map.count( tag ) )
            {
                const fix::value* fld = recvd_map[tag];
                if( val != *fld )
                {
                    std::stringstream ss;
                    ss << "l_expect: value mismatch: " << val << "!=" << *fld;
                    fatal_error( ss.str() );
                }
            }
            else
            {
                std::stringstream ss;
                ss << "l_expect: no such field - " << tag;
                fatal_error( ss.str() );
            }

            lua_pop( l, 1 );

            push_fix( l, recvd_flds );
        }
    }
    else
    {
        fatal_error( "l_expect: timed out" );
    }

    return 1;
}

int l_print( lua_State* l )
{
    fix::fields msg; pop_fix( l, msg );
    print_fix( msg );
    return 0;
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
        core::sleep_ms( 1000 * lua_tointeger( L, -1 ) );
    }

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
        { "print", l_print },
        { "sleep", l_sleep },
        { NULL, NULL }
    };

    luaL_newmetatable( l, INITIATOR_ID );
    luaL_setfuncs( l, reg_fix, 0 );
    lua_pushvalue( l, -1 );
    lua_setfield( l, -1, "__index" );
    lua_setglobal( l, "fix" );
}

int main( int argc, char** argv )
{
    io_thread io;

    lua_State* l = luaL_newstate();
    luaL_openlibs( l );
    l_register( l );

    LOG_INFO( log, "running script: " << argv[1] );

    int err = luaL_dofile( l, argv[1] );
    if( err ) {
        LOG_ERROR( log, "lua error: " << luaL_checkstring( l, -1 ) );
    }

    lua_close( l );

    io.join();
}
