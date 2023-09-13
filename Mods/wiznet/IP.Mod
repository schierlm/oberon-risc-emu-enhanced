MODULE IP; (* jr/16apr18 *)
  IMPORT SYSTEM;

  CONST
    Ok* = 0;

  TYPE
    Adr* = INTEGER;
    (*Packet* = POINTER TO ARRAY 1500 OF BYTE;*)
    Name* = ARRAY 128 OF CHAR;
    Interface* = RECORD
      localAdr*, maskAdr*, gatewayAdr*: Adr;
    END;

  VAR
    res*: INTEGER; (* result of last operation *)
    default: Interface;

  PROCEDURE Put2* (VAR buf: ARRAY OF BYTE; ofs, val: INTEGER);
  BEGIN
    buf[ofs] := val DIV 100H MOD 100H;
    buf[ofs+1] := val MOD 100H
  END Put2;

  PROCEDURE Put4* (VAR buf: ARRAY OF BYTE; ofs, val: INTEGER);
  BEGIN
    buf[ofs] := val DIV 1000000H MOD 100H;
    buf[ofs+1] := val DIV 10000H MOD 100H;
    buf[ofs+2] := val DIV 100H MOD 100H;
    buf[ofs+3] := val MOD 100H
  END Put4;

  PROCEDURE StrToAdr* (ipString: ARRAY OF CHAR): Adr;
    TYPE blobType = RECORD op: INTEGER; res: INTEGER; adr: Adr; name: Name END;
    VAR blob: blobType;
  BEGIN blob.op := 10001H;
    blob.name := ipString;
    SYSTEM.PUT(-32, SYSTEM.ADR(blob)); res := blob.res
  RETURN blob.adr
  END StrToAdr;

  PROCEDURE AdrToStr* (a: Adr; VAR string: ARRAY OF CHAR);
    TYPE blobType = RECORD op: INTEGER; res: INTEGER; adr: Adr; name: Name END;
    VAR blob: blobType; i: INTEGER;
  BEGIN blob.op := 10002H;
    blob.adr := a;
    SYSTEM.PUT(-32, SYSTEM.ADR(blob));
    i := 0; WHILE(blob.name[i] # 0X) DO string[i] := blob.name[i]; INC(i) END;
    string[i] := 0X; res := blob.res
  END AdrToStr;

  PROCEDURE SetInterface* (own, mask, gw: Adr);
  BEGIN (*empty *)
  END SetInterface;

  PROCEDURE OutInterface*;
  BEGIN (*empty *)
  END OutInterface;

END IP.
