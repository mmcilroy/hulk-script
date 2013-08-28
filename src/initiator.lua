
header = {
    [49]="BANZAI",
    [56]="EXEC"
}

body = {
     [98]="0",
    [108]="30"
}

session = fix.new_initiator( "tcp://localhost:8001", "FIX.4.4", header );
session:send( "A", body );
msg = session:recv();

print( "got " .. msg[35] );
