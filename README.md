# osc
### Oscilloscope for WAV files

Windows application that displays uncompressed WAV files in an oscilloscope-like view:

![OSC Screenshot](osc-0.png)

To build on Windows:

    Using a Visual Studio x64 Native Tools Command Prompt window:
    
        m.bat         Debug build
        mr.bat        Non-debug build
    
Usage:
    
    osc input [-i] [-o:n] [-p:n] [-r] [-t]
    
    arguments:
        
        input         The WAV file to view
        -i            Creates PNGs in osc_images\osc-N for each frame shown
        -I            Like -i, but first deletes PNG files in osc_images
        -o:n          Offset; start at n seconds into the WAV file
        -p:n          The period, where n is A through G above middle C
        -r            Ignore prior window position stored in the registry
        -t            Append debugging traces to osc.txt
        -T            Like -t, but first delete osc.txt
        
    mouse:
    
        left-click    move the window
        right-click   context menu
        
    keyboard:
    
        ctrl+c        copy current view to the clipboard
        ctrl+s        saves current view to osc_images\osc-N.png
        Page Up       Zoom out. Increase period by one half step
        Page Down     Zoom in. Decrease period by one half step
        Up Arrow      Increase amplitude
        Down Arrow    Decrease amplitude
        Right Arrow   Shift right in the WAV file
        Left Arror    Shift left in the WAV file
        
    sample usage:
    
        osc myfile.wav
        osc myfile.wav -o:30.2                           # starts the view 30.2 seconds into the WAV file
        osc myfile.wav -p:f                              # sets the time for window width to F above middle C
        osc d:\songs\myfile.wav -T -p:g -o:0.5           # clears tracing file and sets initial period and offset
            
The code for osc is covered under GPL v3.
