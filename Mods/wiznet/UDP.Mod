MODULE UDP; (* jr/16apr18 *)
  IMPORT SYSTEM, IP;

  CONST
    Ok* = IP.Ok;
    PortInUse* = 3501;
    Timeout* = 3502;
    BufferOverflow* = 3503;
    NoInterface* = 3504;
    Closed* = 3505;
    Error* = 9999;
    NilPort* = 0;

  TYPE Socket* = RECORD
    lport*: INTEGER;
    socketid: INTEGER;
  END;

  VAR res*: INTEGER; (* result of last operation *)

  PROCEDURE Open* (VAR s: Socket; lport: INTEGER);
    TYPE blobType = RECORD op, res, socketid, lport: INTEGER; END;
    VAR blob: blobType;
  BEGIN blob.op := 10004H; blob.lport := lport;
    SYSTEM.PUT(-32, SYSTEM.ADR(blob));
    res := blob.res;
    s.lport := blob.lport;
    s.socketid := blob.socketid
  END Open;

  PROCEDURE Send* (s: Socket; fip: IP.Adr; fport: INTEGER; data: ARRAY OF BYTE; ofs, len: INTEGER);
    TYPE blobType = RECORD op, res, socketid, fip, fport, len: INTEGER; buf: ARRAY 1500 OF BYTE END;
    VAR blob: blobType; i: INTEGER;
  BEGIN ASSERT(len <= 1500);
    blob.op := 10006H; blob.socketid := s.socketid;
    blob.fip := fip; blob.fport := fport; blob.len := len;
    FOR i := 0 TO len - 1 DO blob.buf[i] := data[ofs+i] END;
    SYSTEM.PUT(-32, SYSTEM.ADR(blob));
    res := blob.res
  END Send;

  PROCEDURE SendBroadcast* (s: Socket; itf: INTEGER; fport: INTEGER; data: ARRAY OF BYTE; ofs, len: INTEGER);
  BEGIN
    Send(s, -1, fport, data, ofs, len);
  END SendBroadcast;

  PROCEDURE Receive* (s: Socket; VAR data: ARRAY OF BYTE; ofs, size, ms: INTEGER; VAR fip: IP.Adr; VAR fport, len: INTEGER);
    TYPE blobType = RECORD op, res, socketid, fip, fport, len, ms: INTEGER; buf: ARRAY 1500 OF BYTE END;
    VAR blob: blobType; i: INTEGER;
  BEGIN ASSERT(size <= 1500);
    blob.op := 10007H; blob.socketid := s.socketid; blob.ms := ms;
    blob.len := size; SYSTEM.PUT(-32, SYSTEM.ADR(blob));
    len := blob.len; fip := blob.fip; fport := blob.fport;
    ASSERT(len <= size);
    FOR i := 0 TO len - 1 DO data[ofs+i] := blob.buf[i] END;
    res := blob.res
  END Receive;

  PROCEDURE Close* (s: Socket);
    TYPE blobType = RECORD op, res, socketid: INTEGER; END;
    VAR blob: blobType;
  BEGIN blob.op := 10005H; blob.socketid := s.socketid;
    SYSTEM.PUT(-32, SYSTEM.ADR(blob));
    res := blob.res;
  END Close;

END UDP.
