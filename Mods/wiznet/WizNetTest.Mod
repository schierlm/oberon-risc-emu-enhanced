MODULE WizNetTest;
  IMPORT IP, DNS, UDP, TCP, Texts, Oberon;

  VAR log: Texts.Writer;

  PROCEDURE WriteAllBytes(c: TCP.Connection);
  VAR buf: ARRAY 1024 OF BYTE; i, len: INTEGER;
  BEGIN
    REPEAT UNTIL TCP.Available(c) > 0;
    TCP.Receive (c, buf, 0, 1024, 1, len);
    WHILE(TCP.res # TCP.InterfaceClosed) DO
      ASSERT(TCP.res = TCP.Ok);
      FOR i := 0 TO len-1 DO
        IF (buf[i] = 13) OR (buf[i] = 10) OR ((buf[i] >= 32) & (buf[i] < 127)) THEN
          Texts.Write(log, CHR(buf[i]))
        ELSE
          Texts.Write(log, "?")
        END
      END;
      Texts.Append(Oberon.Log, log.buf);
      REPEAT UNTIL TCP.Available(c) > 0;
      TCP.Receive (c, buf, 0, 1024, 1, len);
    END;
  END WriteAllBytes;

  PROCEDURE RunDNSTest*;
    VAR n: IP.Name; a: IP.Adr;
  BEGIN
    Texts.WriteString(log, "Hostname: ");
    DNS.GetHostName(n);
    ASSERT(DNS.res = DNS.Ok);
    Texts.WriteString(log, n);
    Texts.WriteLn(log);
    n := "www.projectoberon.com";
    Texts.WriteString(log, "IP of ");
    Texts.WriteString(log, n);
    Texts.WriteString(log, " = ");
    DNS.HostByName(n, a);
    ASSERT(DNS.res = DNS.Ok);
    IP.AdrToStr(a, n);
    ASSERT(IP.res = IP.Ok);
    Texts.WriteString(log, n);
    Texts.WriteLn(log);
    Texts.WriteString(log, "Reverse DNS of ");
    Texts.WriteString(log, n);
    Texts.WriteString(log, " = ");
    DNS.HostByNumber(a, n);
    ASSERT(DNS.res = DNS.Ok);
    Texts.WriteString(log, n);
    Texts.WriteLn(log);
    n := "1234567890";
    DNS.HostByName(n, a);
    ASSERT(DNS.res = DNS.Ok);
    Texts.WriteString(log, "Numeric host ");
    Texts.WriteString(log, n);
    Texts.WriteString(log, " = ");
    Texts.WriteInt(log, a, 11);
    Texts.WriteLn(log);
    Texts.Append(Oberon.Log, log.buf);
  END RunDNSTest;

  PROCEDURE RunUDPTest*;
    VAR a, fip: IP.Adr; u: UDP.Socket;
      buf: ARRAY 48 OF BYTE; i, fport, len: INTEGER;
  BEGIN
    buf[0] := 23H;
    FOR i := 1 TO 47 DO buf[i] := 0 END;
    DNS.HostByName("ntp.ur.de", a);
    ASSERT(DNS.res = DNS.Ok);
    UDP.Open(u, 0);
    ASSERT(UDP.res = UDP.Ok);
    UDP.Send(u, a, 123, buf, 0, 48);
    ASSERT(UDP.res = UDP.Ok);
    UDP.Receive(u, buf, 0, 48, 1000, fip, fport, len);
    ASSERT(UDP.res = UDP.Ok);
    ASSERT(fip = a);
    ASSERT(fport = 123);
    ASSERT(len = 48);
    UDP.Close(u);
    ASSERT(UDP.res = UDP.Ok);
    ASSERT(buf[0] = 24H);
    Texts.WriteString(log, "SNTP response: Stratum ");
    Texts.WriteInt(log, buf[1], 1);
    Texts.WriteString(log, ", poll interval 2^");
    Texts.WriteInt(log, buf[2], 1);
    Texts.WriteString(log, "s, precision 2^");
    Texts.WriteInt(log, buf[3]-256, 1);
    Texts.WriteString(log, "s.");
    Texts.WriteLn(log);
    Texts.WriteString(log, "    Root delay ");
    Texts.WriteInt(log, buf[4]*1000000H + buf[5] * 10000H + buf[6] * 100H + buf[7], 1);
    Texts.WriteString(log, "/65536 s, Root dispersion ");
    Texts.WriteInt(log, buf[8]*1000000H + buf[9] * 10000H + buf[10] * 100H + buf[11], 1);
    Texts.WriteString(log, "/65536 s");
    Texts.WriteLn(log);
    Texts.WriteString(log, "    Reference identifier ");
    Texts.WriteHex(log, buf[12]*1000000H + buf[13] * 10000H + buf[14] * 100H + buf[15]);
    Texts.WriteLn(log);
    Texts.WriteString(log, "    Reference timestamp ");
    Texts.WriteHex(log, buf[16]*1000000H + buf[17] * 10000H + buf[18] * 100H + buf[19]);
    Texts.WriteHex(log, buf[20]*1000000H + buf[21] * 10000H + buf[22] * 100H + buf[23]);
    Texts.WriteLn(log);
    Texts.WriteString(log, "    Originate timestamp ");
    Texts.WriteHex(log, buf[24]*1000000H + buf[25] * 10000H + buf[26] * 100H + buf[27]);
    Texts.WriteHex(log, buf[28]*1000000H + buf[29] * 10000H + buf[30] * 100H + buf[31]);
    Texts.WriteLn(log);
    Texts.WriteString(log, "    Receive timestamp ");
    Texts.WriteHex(log, buf[32]*1000000H + buf[33] * 10000H + buf[34] * 100H + buf[35]);
    Texts.WriteHex(log, buf[36]*1000000H + buf[37] * 10000H + buf[38] * 100H + buf[39]);
    Texts.WriteLn(log);
    Texts.WriteString(log, "    Transmit timestamp ");
    Texts.WriteHex(log, buf[40]*1000000H + buf[41] * 10000H + buf[42] * 100H + buf[43]);
    Texts.WriteHex(log, buf[44]*1000000H + buf[45] * 10000H + buf[46] * 100H + buf[47]);
    Texts.WriteLn(log);
    Texts.Append(Oberon.Log, log.buf);
  END RunUDPTest;

  PROCEDURE CopyString(VAR dest: ARRAY OF BYTE; src: ARRAY OF CHAR);
  VAR i: INTEGER;
  BEGIN i := 0;
    WHILE (src[i] # 0X) DO dest[i] := ORD(src[i]); INC(i) END
  END CopyString;

  PROCEDURE RunTCPTest*;
    VAR a: IP.Adr; c: TCP.Connection;
      buf: ARRAY 27 OF BYTE;
  BEGIN

    (* TCP test (DAYTIME) *)
    DNS.HostByName("utcnist.colorado.edu", a);
    ASSERT(DNS.res = DNS.Ok);
    TCP.Open(c, 0, a, 13);
    ASSERT(TCP.res = TCP.Ok);
    Texts.WriteString(log, "DAYTIME response from utcnist.colorado.edu: ");
    Texts.WriteLn(log);
    WriteAllBytes(c);
    Texts.WriteLn(log);
    Texts.WriteLn(log);
    TCP.Close(c);
    ASSERT(TCP.res = TCP.Ok);
    Texts.Append(Oberon.Log, log.buf);

    (* TCP test (GOPHER) *)
    DNS.HostByName("gopherpedia.com", a);
    ASSERT(DNS.res = DNS.Ok);
    TCP.Open(c, 0, a, 70);
    ASSERT(TCP.res = TCP.Ok);
    CopyString(buf, "Oberon (operating system)");
    buf[25] := 13; buf[26] := 10;
    TCP.Send(c, buf, 0, 27, TRUE);
    ASSERT(TCP.res = TCP.Ok);
    Texts.WriteString(log, "GopherPedia article about Oberon: ");
    Texts.WriteLn(log);
    WriteAllBytes(c);
    Texts.WriteLn(log);
    Texts.WriteLn(log);
    TCP.Close(c);
    ASSERT(TCP.res = TCP.Ok);
    Texts.Append(Oberon.Log, log.buf);
  END RunTCPTest;

  PROCEDURE RunListenTest*;
    VAR a: IP.Adr; l, c: TCP.Connection;
      buf: ARRAY 27 OF BYTE;
  BEGIN
    TCP.Open(l, 10001, 0, 0);
    ASSERT(TCP.res = TCP.Ok);
    REPEAT UNTIL TCP.Accept (l, c) # TCP.NotConnected;
    ASSERT(TCP.res = TCP.Ok);
    TCP.Close(l);
    ASSERT(TCP.res = TCP.Ok);
    Texts.WriteString(log, "Response from listening on port 10001: ");
    Texts.WriteLn(log);
    WriteAllBytes(c);
    Texts.WriteLn(log);
    Texts.WriteLn(log);
    TCP.Close(c);
    ASSERT(TCP.res = TCP.Ok);
    Texts.Append(Oberon.Log, log.buf);
  END RunListenTest;

  PROCEDURE Run*;
  BEGIN
    RunDNSTest; RunUDPTest; RunTCPTest
  END Run;

BEGIN
  Texts.OpenWriter(log)
END WizNetTest.
