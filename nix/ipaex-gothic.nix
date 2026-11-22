{ ipaexfont, runCommand }:
runCommand "ipaex-gothic-${ipaexfont.version}" { } ''
  install -Dm444 ${ipaexfont}/share/fonts/truetype/ipaexg.ttf \
    "$out/share/fonts/truetype/ipaexg.ttf"
''
