-- This script is used to generate a .DS_Store customizing how the .dmg looks

on run argv
  set image_name to item 1 of argv

  tell application "Finder"
    tell disk image_name
      open
        set current view of container window to icon view
        set theViewOptions to the icon view options of container window
        set arrangement of theViewOptions to not arranged
        set icon size of theViewOptions to 52

        -- make alias can't create a link to an nonexistent file?
        set currentDir to target of container window as alias
        do shell script "ln -s '/Library/Application Support/REAPER/UserPlugins' " ¬
         & quoted form of POSIX path of currentDir

        tell container window
          set sidebar   width   to 0
          set statusbar visible to false
          set toolbar   visible to false
          set the bounds        to { 150, 150, 714, 714 }

          set reaper_plugin to "reaper_reallm-x86_64.dylib"

          if not exists item reaper_plugin
           -- set reaper_plugin to "reaper_*-arm64.dylib"
          end if

          if not exists item reaper_plugin
           -- set reaper_sws to "reaper_*-i386.dylib"
          end if

          set position of item reaper_plugin      to { 124, 85  }
          set position of item "UserPlugins"   to { 421, 85  }
        end tell

        update without registering applications
      close
    end tell
  end tell
end run