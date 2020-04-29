MODULE DNS; (* jr/16apr18 *)
  IMPORT SYSTEM, IP;

  CONST
    Ok* = IP.Ok;
    NotFound* = 3601;
    BadName* = 3602;
    MaxNofServer* = 4;

  TYPE
    Name* = IP.Name;

  VAR
    domain*: Name;
    res*: INTEGER; (* result of last operation *)

  PROCEDURE HostByName* (hostname: ARRAY OF CHAR; VAR addr: IP.Adr);
  BEGIN
    addr := IP.StrToAdr(hostname); res := IP.res;
  END HostByName;

  PROCEDURE HostByNumber* (addr: IP.Adr; VAR hostname: ARRAY OF CHAR);
    TYPE blobType = RECORD op: INTEGER; res: INTEGER; adr: IP.Adr; name: Name END;
    VAR blob: blobType; i: INTEGER;
  BEGIN blob.op := 10003H;
    blob.adr := addr;
    SYSTEM.PUT(-32, SYSTEM.ADR(blob));
    i := 0; WHILE(blob.name[i] # 0X) DO hostname[i] := blob.name[i]; INC(i) END;
    hostname[i] := 0X; res := blob.res
  END HostByNumber;

  PROCEDURE GetHostName* (VAR hostname: ARRAY OF CHAR);
  BEGIN
    hostname := "emulated-wiznet";
  END GetHostName;

END DNS.
