MODULE Display;  (*autodetect color or b/w*)
  IMPORT SYSTEM, DisplayM, DisplayC;

  CONST black* = 0; white* = 1;  (*black = background*)
    replace* = 0; paint* = 1; invert* = 2;  (*modes*)

  TYPE Frame* = POINTER TO FrameDesc;
    FrameMsg* = RECORD END ;
    Handler* = PROCEDURE (F: Frame; VAR M: FrameMsg);
    FrameDesc* = RECORD next*, dsc*: Frame;
        X*, Y*, W*, H*: INTEGER;
        handle*: Handler
      END ;

  VAR Base*, Width*, Height*: INTEGER;
    arrow*, star*, hook*, updown*, block*, cross*, grey*: INTEGER;
    color: BOOLEAN;

  PROCEDURE Handle*(F: Frame; VAR M: FrameMsg);
  BEGIN
    IF (F # NIL) & (F.handle # NIL) THEN F.handle(F, M) END
  END Handle;

  (* raster ops *)

  PROCEDURE Dot*(col, x, y, mode: INTEGER);
  BEGIN IF color THEN DisplayC.Dot(col, x, y, mode) ELSE DisplayM.Dot(col, x, y, mode) END
  END Dot;

  PROCEDURE ReplConst*(col, x, y, w, h, mode: INTEGER);
  BEGIN IF color THEN DisplayC.ReplConst(col, x, y, w, h, mode) ELSE DisplayM.ReplConst(col, x, y, w, h, mode) END
  END ReplConst;

  PROCEDURE CopyPattern*(col, patadr, x, y, mode: INTEGER);
  BEGIN IF color THEN DisplayC.CopyPattern(col, patadr, x, y, mode) ELSE DisplayM.CopyPattern(col, patadr, x, y, mode) END
  END CopyPattern;

  PROCEDURE CopyBlock*(sx, sy, w, h, dx, dy, mode: INTEGER);
  BEGIN IF color THEN DisplayC.CopyBlock(sx, sy, w, h, dx, dy, mode) ELSE DisplayM.CopyBlock(sx, sy, w, h, dx, dy, mode) END
  END CopyBlock;

  PROCEDURE ReplPattern*(col, patadr, x, y, w, h, mode: INTEGER);
  BEGIN IF color THEN DisplayC.ReplPattern(col, patadr, x, y, w, h, mode) ELSE DisplayM.ReplPattern(col, patadr, x, y, w, h, mode) END
  END ReplPattern;

BEGIN
  SYSTEM.GET(0E7F00H, arrow);
  IF arrow = 436F537AH THEN
    color := TRUE;
    Base := DisplayC.Base;
    Width := DisplayC.Width;
    Height := DisplayC.Height;
    arrow := DisplayC.arrow;
    star := DisplayC.star;
    hook := DisplayC.hook;
    updown := DisplayC.updown;
    block := DisplayC.block;
    cross := DisplayC.cross;
    grey := DisplayC.grey
  ELSE
    color := FALSE;
    Base := DisplayM.Base;
    Width := DisplayM.Width;
    Height := DisplayM.Height;
    arrow := DisplayM.arrow;
    star := DisplayM.star;
    hook := DisplayM.hook;
    updown := DisplayM.updown;
    block := DisplayM.block;
    cross := DisplayM.cross;
    grey := DisplayM.grey
  END
END Display.
