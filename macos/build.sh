#!/bin/sh -e
CONTENTS=dmg/OberonRiscEmulator.app/Contents
FRAMEWORKS=$CONTENTS/Frameworks
mkdir -p ${FRAMEWORKS}
mkdir ${CONTENTS}/MacOS ${CONTENTS}/Resources dmg/.background
curl -L https://github.com/libsdl-org/SDL/releases/download/release-2.28.1/SDL2-2.28.1.dmg -o SDL2.dmg
hdiutil attach SDL2.dmg
cp -a /Volumes/SDL2/SDL2.framework ${FRAMEWORKS}
hdiutil detach /Volumes/SDL2
cc -o risc_x64 -target x86_64-apple-macos10.7 -framework SDL2 -F ${FRAMEWORKS} src/*.c -I ${FRAMEWORKS}/SDL2.framework/Headers
cc -o risc_arm64 -target arm64-apple-macos11 -framework SDL2 -F ${FRAMEWORKS} src/*.c -I ${FRAMEWORKS}/SDL2.framework/Headers
lipo -create -output ${CONTENTS}/MacOS/risc risc_x64 risc_arm64
install_name_tool -add_rpath '@executable_path/../Frameworks' ${CONTENTS}/MacOS/risc
cp macos/Info.plist ${CONTENTS}
cp macos/icon.icns ${CONTENTS}/Resources
cp macos/background.png dmg/.background/background.png
ln -s /Applications dmg/Applications
cp macos/RiscLauncher pc*.sh ${CONTENTS}/MacOS
hdiutil create -srcfolder "dmg" -volname "OberonRiscEmulator" -fs HFS+ -fsargs "-c c=64,a=16,e=16" -format UDRW -size 9500k temp.dmg
hdiutil attach -readwrite -noverify -noautoopen temp.dmg
echo '
   tell application "Finder"
     tell disk "OberonRiscEmulator"
           open
           set current view of container window to icon view
           set toolbar visible of container window to false
           set statusbar visible of container window to false
           set the bounds of container window to {400, 100, 760, 220}
           set theViewOptions to the icon view options of container window
           set arrangement of theViewOptions to not arranged
           set icon size of theViewOptions to 72
           set background picture of theViewOptions to file ".background:background.png"
           set position of item "OberonRiscEmulator.app" of container window to {20, 60}
           set position of item "Applications" of container window to {190, 60}
           update without registering applications
           delay 2
           close
     end tell
   end tell
' | osascript
chmod -Rf go-w /Volumes/OberonRiscEmulator
sync
sync
hdiutil detach /Volumes/OberonRiscEmulator
hdiutil convert temp.dmg -format UDZO -imagekey zlib-level=9 -o OberonRiscEmulator.dmg
rm -f temp.dmg
