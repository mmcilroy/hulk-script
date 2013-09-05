
header = {
     [49]="CLIENT1",
     [50]="CLIENT1_S",
     [56]="CITISVR"
}

body = {
      [1]="MARK",
     [11]=fix.uuid(),
     [38]="100",
     [40]="1",
     [44]="market + 0 t",
     [54]="1",
     [55]="VOD.L",
     [59]="0",
    [423]="1",
    [528]="A",
    [847]="DMAModel",
    [848]="VENUE:Aggregation",
   [7100]="Aggregation",
   [8004]="MARK",
  [10005]="1",
  [10039]="MARK",
  [11210]="ROOTORDER",
  [10515]="162",
  [10201]="FLOWENTRY",
  [10292]="FLOWDESK",
  [10202]="FLOWCAT",
  [10203]="FLOWCLASS"
}

function nos( session, qty ) 
    fix.sleep( 5 );
    body[11] = fix.uuid();
    body[38] = qty;
    session:send( "D", body );
end

session = fix.new_initiator( "tcp://uk-vm004:8061", "FIX.4.4", header );

nos( session, 4 );
nos( session, 50 );
nos( session, 50000 );

while 1 do
    msg = session:recv();
    if msg ~= nil then
        if msg[150] == "0" then
            print( "ack: " .. msg[11] );
        end
        if msg[150] == "F" then
            print( "fill: " .. msg[11] .. ", " .. msg[32] );
        end
    end
end
