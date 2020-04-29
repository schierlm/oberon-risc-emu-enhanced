MODULE TCP; (* 16apr18 *)
  IMPORT SYSTEM, IP;
  CONST
    NilPort* = 0;
    Ok* = IP.Ok;
    ConnectionRefused* = 3701;
    ConnectionReset* = 3702;
    WrongInterface* = 3703;
    TimedOut* = 3704;
    NotConnected* = 3705;
    NoInterface* = 3706;
    InterfaceClosed* = 3707;

    Listen* = 1;
    Established* = 4;

  TYPE Connection* = RECORD
      int: IP.Interface;
      lport: INTEGER;
      fip: IP.Adr;
      fport: INTEGER;
      state: BYTE;
      socket: INTEGER;
      localAdr, foreignAdr: INTEGER;
    END;

  VAR res*: INTEGER; (* result of last operation *)

  PROCEDURE Open* (VAR c: Connection; lport: INTEGER; fip: IP.Adr; fPort: INTEGER);
    TYPE blobType = RECORD op, res, socketid, lport, fip, fport: INTEGER; END;
    VAR blob: blobType;
  BEGIN blob.op := 10008H; blob.lport := lport; blob.fip := fip; blob.fport := fPort;
    SYSTEM.PUT(-32, SYSTEM.ADR(blob));
    res := blob.res;
    c.lport := blob.lport;
    c.fip := blob.fip;
    c.fport := blob.fport;
    c.state := Established;
    IF (fip = 0) & (fPort = 0) THEN c.state := Listen END;
    c.socket := blob.socketid;
    c.localAdr := 0;
    c.foreignAdr := fip
  END Open;

  PROCEDURE SendChunk (c: Connection; data: ARRAY OF BYTE; ofs, len: INTEGER; propagate: BOOLEAN);
    TYPE blobType = RECORD op, res, socketid, len, flush: INTEGER; buf: ARRAY 1500 OF BYTE END;
    VAR blob: blobType; i: INTEGER;
  BEGIN ASSERT(len <= 1500);
    blob.op := 10009H; blob.socketid := c.socket; blob.len := len; blob.flush := ORD(propagate);
    FOR i := 0 TO len - 1 DO blob.buf[i] := data[ofs+i] END;
    SYSTEM.PUT(-32, SYSTEM.ADR(blob));
    res := blob.res
  END SendChunk;

  PROCEDURE Send* (c: Connection; data: ARRAY OF BYTE; ofs, len: INTEGER; propagate: BOOLEAN);
  BEGIN res := 0;
    WHILE (res = 0) & (len > 1500) DO
      SendChunk(c, data, ofs, 1500, FALSE);
      INC(ofs, 1500);
      DEC(len, 1500)
    END;
    IF res = 0 THEN SendChunk(c, data, ofs, len, propagate) END
  END Send;

  PROCEDURE ReceiveChunk (c: Connection; VAR data: ARRAY OF BYTE; ofs, size, min: INTEGER; VAR len: INTEGER);
    TYPE blobType = RECORD op, res, socketid, len, minlen: INTEGER; buf: ARRAY 1500 OF BYTE END;
    VAR blob: blobType; i: INTEGER;
  BEGIN ASSERT(min <= 1500);
    blob.op := 1000AH; blob.socketid := c.socket; blob.len := size; blob.minlen := min;
    IF size > 1500 THEN blob.len := 1500 END;
    SYSTEM.PUT(-32, SYSTEM.ADR(blob));
    len := blob.len;
    FOR i := 0 TO len - 1 DO data[ofs+i] := blob.buf[i] END;
    res := blob.res
  END ReceiveChunk;

  PROCEDURE Receive* (c: Connection; VAR data: ARRAY OF BYTE; ofs, size, min: INTEGER; VAR len: INTEGER);
    VAR realLen: INTEGER;
  BEGIN res := 0; realLen := 0;
    WHILE (res = 0) & (min > 1500) DO
      ReceiveChunk(c, data, ofs, 1500, 1500, len);
      realLen := realLen + 1500;
      INC(ofs, 1500);
      DEC(len, 1500);
      DEC(min, 1500);
    END;
    IF res = 0 THEN ReceiveChunk(c, data, ofs, size, min, len) ELSE len := 0 END;
    len := len + realLen
  END Receive;

  PROCEDURE DelaySend* (c: Connection; enable: BOOLEAN);
  BEGIN (* no-op *)
  END DelaySend;

  PROCEDURE KeepAlive* (c: Connection; enable: BOOLEAN);
  BEGIN (* no-op *)
  END KeepAlive;

  PROCEDURE Available* (c: Connection): INTEGER;
    TYPE blobType = RECORD op, res, socketid: INTEGER END;
    VAR blob: blobType;
  BEGIN
    blob.op := 1000BH; blob.socketid := c.socket;
    SYSTEM.PUT(-32, SYSTEM.ADR(blob));
    res := 0
  RETURN blob.res
  END Available;

  PROCEDURE State* (c: Connection): INTEGER;
    VAR result: INTEGER;
  BEGIN result := c.state;
  RETURN result
  END State;

  PROCEDURE AwaitState* (c: Connection; good, bad: SET; ms: INTEGER);
  BEGIN (* no-op *)
  END AwaitState;

  PROCEDURE Close* (c: Connection);
    TYPE blobType = RECORD op, res, socketid: INTEGER END;
    VAR blob: blobType;
  BEGIN
    blob.op := 1000CH; blob.socketid := c.socket;
    SYSTEM.PUT(-32, SYSTEM.ADR(blob));
    res := blob.res
  END Close;

  PROCEDURE Accept* (c: Connection; VAR client: Connection): INTEGER;
    TYPE blobType = RECORD op, res, socketid, clientid, fip, fport: INTEGER END;
    VAR blob: blobType;
  BEGIN
    blob.op := 1000DH; blob.socketid := c.socket;
    SYSTEM.PUT(-32, SYSTEM.ADR(blob));
    res := blob.res;
    IF (res = Ok) & (blob.clientid = -1) THEN
      res := NotConnected;
    END;
    IF res = Ok THEN
      client.lport := c.lport;
      client.fip := blob.fip;
      client.fport := blob.fport;
      client.state := Established;
      client.socket := blob.clientid;
      client.localAdr := 0;
      client.foreignAdr := blob.fip;
    END;
    RETURN res
  END Accept;

END TCP.
