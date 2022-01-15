//
// Oscilloscope for WAV files
//

#include <windows.h>
#include <gdiplus.h>

#include <stdio.h>
#include <math.h>
#include <ppl.h>

using namespace std;
using namespace Gdiplus;
using namespace concurrency;

#include <djltrace.hxx>
#include <djl_wav.hxx>
#include <djlres.hxx>
#include <djltimed.hxx>
#include <djlsav.hxx>
#include <djlenum.hxx>

#include "osc.hxx"

#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "gdiplus.lib" )
#pragma comment( lib, "advapi32.lib" )
#pragma comment( lib, "user32.lib" )
#pragma comment( lib, "gdi32.lib" )
#pragma comment( lib, "shell32.lib" )

#define REGISTRY_APP_NAME L"SOFTWARE\\davidlyosc"
#define REGISTRY_WINDOW_POSITION L"WindowPosition"

CDJLTrace tracer;
DjlParseWav * g_pwav = 0;
double g_secondsOffset = 0;
double g_wavSeconds = 0.0;
double g_viewPeriod = 0.0;
double g_amplitudeZoom = 1.0;
DWORD g_wavSamples = 0;
int g_notePeriod = 'a';          // valid values: 'a'..'g'
double g_currentPeriod = 440.0;  // maps to A above middle C
HFONT g_fontText = 0;
int g_fontHeight = 0;
int g_borderSize = 0;
bool g_createImages = false;
const WCHAR * g_imagesFolder = L"osc_images";

const int g_waveformWindowSize = 969; // nice. needs to be odd.
const WORD g_maxChannels = 16;

long long timeSetPixels = 0;
long long timeBorderText = 0;
long long timeBorderLines = 0;
long long timeBlt = 0;

int PeriodIndex()
{
    return g_notePeriod - 'a';
} //PeriodIndex

double PeriodFrequency()
{
    // 0 is A above middle C, which is 440 Hz

    return 440.0 * pow( 1.0594630943592952645618252949463, PeriodIndex() );
} //PeriodFrequency

void UpdateCurrentPeriod()
{
    g_currentPeriod = PeriodFrequency();
    g_viewPeriod = 1.0 / g_currentPeriod;
} //UpdateCurrentPeriod

const WCHAR * NoteToString()
{
    const WCHAR *aNotes[] =
    {
        L"A ", L"A#", L"B ", L"C ", L"C#", L"D ", L"D#", L"E ", L"F ", L"F#", L"G ", L"G#", 
    };

    assert( 12 == _countof( aNotes ) );
    int index = PeriodIndex() % 12;

    // 0 is A above middle C
    // tracer.Trace( "note %u is %ws\n", index, aNotes[ index >= 0 ? index : 12 + index ] );

    if ( index >= 0 )
        return aNotes[ index ];

    return aNotes[ 12 + index ];
} //NoteToString

void DeleteFolder( const WCHAR * folder )
{
    CStringArray paths;
    CEnumFolder enumFolder( true, &paths, NULL, 0 );
    enumFolder.Enumerate( folder, L"*.png" ); // just to be safe

    for ( size_t i = 0; i < paths.Count(); i++ )
        DeleteFile( paths[ i ] );

    RemoveDirectory( folder );
} //DeleteFolder

int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow )
{
    SetProcessDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );

    bool enableTracer = false;
    bool emptyTracerFile = false;
    bool readPosFromReg = true;
    static WCHAR awcInput[MAX_PATH] = {};

    {
        int argc = 0;
        LPWSTR * argv = CommandLineToArgvW( GetCommandLineW(), &argc );

        for ( int i = 1; i < argc; i++ )
        {
            const WCHAR * pwcArg = argv[ i ];
            WCHAR a0 = pwcArg[ 0 ];

            if ( ( L'-' == a0 ) || ( L'/' == a0 ) )
            {
               WCHAR a1 = towlower( pwcArg[1] );

               if ( 'i' == a1 )
               {
                   if ( 'I' == pwcArg[1] )
                       DeleteFolder( g_imagesFolder );

                   g_createImages = true;
               }
               else if ( 'r' == a1 )
                   readPosFromReg = false;
               else if ( 't' == pwcArg[1] )
                   enableTracer = true;
               else if ( 'T' == pwcArg[1] )
               {
                   enableTracer = true;
                   emptyTracerFile = true;
               }
               else if ( 'o' == a1 )
               {
                   if ( ':' != pwcArg[2] )
                       return 0;

                   g_secondsOffset = fabs( wcstof( pwcArg + 3, NULL ) );
               }
               else if ( 'p' == a1 )
               {
                   if ( ':' != pwcArg[2] )
                       return 0;

                   char a = tolower( pwcArg[3] );

                   if ( a > 'g' || a < 'a' )
                       return 0;

                   g_notePeriod = a;
                   UpdateCurrentPeriod();
               }
            }
            else
            {
                if ( wcslen( pwcArg ) < _countof( awcInput ) )
                    wcscpy_s( awcInput, _countof( awcInput ), pwcArg );
            }
        }

        LocalFree( argv );
    }

    tracer.Enable( enableTracer, L"osc.txt", emptyTracerFile );
    tracer.Trace( "g_secondsOffset: %lf\n", g_secondsOffset );

    if ( 0 == awcInput[0] )
    {
        tracer.Trace( "no wav file specified\n" );
        MessageBox( NULL, L"No WAV file specified on the command line.", L"error", MB_OK );
        return 0;
    }

    DjlParseWav parseWav( awcInput );
    g_pwav = &parseWav;
    if ( !parseWav.SuccessfulParse() )
    {
        tracer.Trace( "can't parse wav file %ws\n", awcInput );
        MessageBox( NULL, L"Error parsing the WAV file.", L"error", MB_OK );
        return 0;
    }

    DjlParseWav::WavSubchunk &fmt = parseWav.GetFmt();
    g_wavSamples = parseWav.Samples();
    g_wavSeconds = parseWav.SecondsOfSound();
    g_secondsOffset = __min( g_secondsOffset, g_wavSeconds );
    UpdateCurrentPeriod();

    HRESULT hr = CoInitializeEx( NULL, COINIT_MULTITHREADED );
    if ( FAILED( hr ) )
    {
        tracer.Trace( "can't initialize COM: %#x\n", hr );
        return 0;
    }

    GdiplusStartupInput si = {};
    ULONG_PTR gdiplusToken = 0;
    Status gdiStatus = GdiplusStartup( &gdiplusToken, &si, NULL );
    if ( Status::Ok != gdiStatus )
    {
        tracer.Trace( "can't initialize Gdiplus: %#x\n", gdiStatus );
        return 0;
    }

    RECT rectDesk;
    GetWindowRect( GetDesktopWindow(), &rectDesk );
    g_fontHeight = (int) round( ( (double) __min( rectDesk.right, rectDesk.bottom ) ) * 0.012 );
    g_borderSize = g_fontHeight + 1;
    const int windowDimensions = g_waveformWindowSize + ( 2 * g_borderSize );
    assert( windowDimensions & 1 ); // it needs to be odd

    g_fontText = CreateFont( g_fontHeight, 0, 0, 0, FW_THIN, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_OUTLINE_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH, L"Consolas" ); // L"Cascadia Mono Semilight" ); //NULL );
    int posLeft = 380;
    int posTop = 100;

    if ( readPosFromReg )
    {
        WCHAR awcPos[ 100 ];
        BOOL fFound = CDJLRegistry::readStringFromRegistry( HKEY_CURRENT_USER, REGISTRY_APP_NAME, REGISTRY_WINDOW_POSITION, awcPos, sizeof( awcPos ) );
        if ( fFound )
            swscanf( awcPos, L"%d %d", &posLeft, &posTop );
    }

    LRESULT CALLBACK WindowProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam );
    const WCHAR CLASS_NAME[] = L"OSC-davidly-Class";
    WNDCLASS wc = {};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor( NULL, IDC_ARROW );
    wc.hIcon         = LoadIcon( hInstance, MAKEINTRESOURCE( 100 ) ) ;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH) GetStockObject(HOLLOW_BRUSH);
    RegisterClass( &wc );

    HWND hwnd = CreateWindowEx( WS_EX_TOOLWINDOW, CLASS_NAME, L"Oscilloscope", WS_POPUP, posLeft, posTop,
                                windowDimensions, windowDimensions, NULL, NULL, hInstance, NULL );
    if ( NULL == hwnd )
    {
        tracer.Trace( "can't create window: %d\n", GetLastError() );
        return 0;
    }

    ShowWindow( hwnd, nCmdShow );
    SetProcessWorkingSetSize( GetCurrentProcess(), ~0, ~0 );

    MSG msg = {};
    while ( GetMessage( &msg, NULL, 0, 0 ) )
    {
        TranslateMessage( &msg );
        DispatchMessage( &msg );
    }

    GdiplusShutdown( gdiplusToken );
    CoUninitialize();

    tracer.Trace( "time to set pixels    %lld ms\n", timeSetPixels / CTimed::NanoPerMilli() );
    tracer.Trace( "time for border text  %lld ms\n", timeBorderText / CTimed::NanoPerMilli() );
    tracer.Trace( "time for border lines %lld ms\n", timeBorderLines / CTimed::NanoPerMilli() );
    tracer.Trace( "time to blt           %lld ms\n", timeBlt / CTimed::NanoPerMilli() );

    return 0;
} //wWinMain

__forceinline DWORD SampleToY( double l, double halfBottom )
{
    double ol = 1.0 + ( l * g_amplitudeZoom );
    ol = 2.0 - ol; // flip because y of 0 is at the top in Windows

    // ol now in range 0.0 .. 2.0 (if amplitude is 1.0)
    return (DWORD) round( ol * halfBottom );
} //SampleToY

__forceinline bool InWaveformRange( DWORD y, DWORD waveformBottom )
{
    // amplified waveforms can be outside of this range, and that must be clipped
    return ( ( y >= g_borderSize ) && ( y < waveformBottom ) );
} //InWaveformRange

void RenderTextToDC( HDC hdc, RECT & rect )
{
    CTimed timedBorderText( timeBorderText );
    DjlParseWav::WavSubchunk &fmt = g_pwav->GetFmt();
    HFONT fontOld = (HFONT) SelectObject( hdc, g_fontText );
    COLORREF crOld = SetBkColor( hdc, 0 );
    COLORREF crTextOld = SetTextColor( hdc, 0x00ff00 );
    UINT taOld = SetTextAlign( hdc, TA_CENTER );

    static WCHAR awcText[ 100 ] = {};
    swprintf_s( awcText, _countof( awcText ), L"period %wc%wc %lf %ws    amplitude %wc%wc %2.1lf    offset %wc%wc %lf",
                0x25b2, 0x25bc, g_viewPeriod, NoteToString(), 0x2191, 0x2193, g_amplitudeZoom, 0x2190, 0x2192, g_secondsOffset );
    
    int len = wcslen( awcText );
    RECT rectTopText = rect;
    rectTopText.bottom = g_fontHeight;
    ExtTextOut( hdc, rectTopText.right / 2, 0, ETO_OPAQUE, &rectTopText, awcText, len, NULL );

    // The bottom text never changes; compute it once
    static WCHAR awcWav[ 100 ] = {};
    if ( 0 == awcWav[0] )
        swprintf_s( awcWav, _countof( awcWav ), L"format %ws    channels %d    rate %d    bps %d    seconds %lf",
                    g_pwav->GetFormatType(), fmt.channels, fmt.sampleRate, fmt.bitsPerSample, (double) g_pwav->Samples() / (double) fmt.sampleRate );

    len = wcslen( awcWav );
    RECT rectBottomText = rect;
    rectBottomText.top = rectBottomText.bottom - g_fontHeight;
    ExtTextOut( hdc, rectBottomText.right / 2, rectBottomText.top, ETO_OPAQUE, &rectBottomText, awcWav, len, NULL );

    SetTextAlign( hdc, taOld );
    SetTextColor( hdc, crTextOld );
    SetBkColor( hdc, crOld );
    SelectObject( hdc, fontOld );
} //RenderTextToDC

void RenderBorderToDC( HDC hdc, RECT & rect )
{
    CTimed timedBorderLines( timeBorderLines );
    Graphics graphics( hdc );
    SolidBrush brush( Color( 255, 0, 255, 0 ) ); // green
    Pen pen( &brush, 1.0 );

    assert( rect.bottom & 0x1 );
    assert( rect.right == rect.bottom );
    const int bs = g_borderSize;
    const int bsm1 = g_borderSize - 1;
    const int dim = rect.right;
    const int dimm1 = dim - 1;
    const int half = dimm1 / 2;

    struct Line { int x, y, x2, y2; };
    static const Line lines[] =
    {
        0,            half,         bsm1,         half,
        dim - bs,     half,         dimm1,        half,
        0,            bsm1,         dimm1,        bsm1,
        0,            dimm1 - bsm1, dimm1,        dimm1 - bsm1,
        bsm1,         0,            bsm1,         dimm1,
        dimm1 - bsm1, 0,            dimm1 - bsm1, dimm1,
    };

    for ( int l = 0; l < _countof( lines ); l++ )
        graphics.DrawLine( &pen, lines[l].x, lines[l].y, lines[l].x2, lines[l].y2 );
} //RenderBorderToDC

void RenderToDC( HDC hdc, RECT & rect )
{
    const DjlParseWav::WavSubchunk & fmt = g_pwav->GetFmt();
    const DWORD shownSamples = (DWORD) round( (double) fmt.sampleRate * (double) g_viewPeriod );
    const DWORD firstSample = (DWORD) round( g_secondsOffset * (double) fmt.sampleRate );
    const DWORD lastSample = __min( firstSample + shownSamples, g_wavSamples );
    const double xFactor = (double) ( rect.right - 2 * g_borderSize - 1 ) / (double) shownSamples;
    const double halfBottom = (double) ( ( rect.bottom - 1 ) - 2 * g_borderSize ) / 2.0;
    const COLORREF channelColors[ g_maxChannels ] = { 0xffffff, 0xff0000, 0x00ff00, 0xffff00,
                                                      0xcc0000, 0x00cc00, 0x0000cc, 0xcccc00,
                                                      0x880000, 0x008800, 0x000088, 0x888800,
                                                      0x440000, 0x004400, 0x000044, 0x444400 };

    //tracer.Trace( "g_viewPeriod %.10lf, seconds %lf, shownSamples: %u\n", g_viewPeriod, g_wavSeconds, shownSamples );

    Bitmap bmBack( rect.right, rect.bottom, PixelFormat32bppRGB );
    Rect rectG( 0, 0, rect.right, rect.bottom );
    BitmapData bd = {};
    Status status = bmBack.LockBits( &rectG, ImageLockModeRead | ImageLockModeWrite, PixelFormat32bppRGB, &bd );
    if ( Status::Ok == status )
    {
        CTimed timedSetPixels( timeSetPixels );
        const int stride = abs( bd.Stride );
        const int strideby4 = stride / 4;
        byte * pb = (byte *) bd.Scan0;
    
        if ( bd.Stride < 0 )
            pb += ( bd.Stride * ( rect.bottom - 1 ) );
    
        ULONG * pbuf = (ULONG *) pb;
        const DWORD waveformBottom = rect.bottom - g_borderSize;
        WORD channelCount = __min( g_pwav->Channels(), _countof( channelColors ) );
        const DWORD invalidY = 0xffffffff;
    
        parallel_for( firstSample, lastSample, [&] ( DWORD s )
        {
            DWORD x = g_borderSize + (DWORD) round( (double) ( s - firstSample ) * xFactor );
            DWORD yval[ g_maxChannels ];

            for ( WORD ch = 0; ch < channelCount; ch++ )
            {
                double v = g_pwav->GetSampleInChannel( s, ch );
                DWORD yv = g_borderSize + SampleToY( v, halfBottom );
                if ( InWaveformRange( yv, waveformBottom ) )
                    yval[ ch ] = yv;
                else
                    yval[ ch ] = invalidY;
            }

            for ( WORD ch = 0; ch < channelCount; ch++ )
            {
                if ( yval[ ch ] != invalidY )
                {
                    bool anyDuplicates = false;
                    for ( WORD dc = 0; dc < channelCount; dc++ )
                    {
                        if ( dc != ch && yval[ ch ] == yval[ dc ] )
                        {
                            anyDuplicates = true;
                            break;
                        }
                    }

                    pbuf[ strideby4 * yval[ ch ] + x ] = ( anyDuplicates ) ? 0xff : channelColors[ ch ];
                }
            }
        } );
    
        status = bmBack.UnlockBits( &bd );
        timedSetPixels.Complete();

        HDC hdcBack = CreateCompatibleDC( hdc );
        HBITMAP bmpBack;
        bmBack.GetHBITMAP( 0, &bmpBack );
        HBITMAP bmpOld = (HBITMAP) SelectObject( hdcBack, bmpBack );
    
        RenderTextToDC( hdcBack, rect );
        RenderBorderToDC( hdcBack, rect );

        CTimed timedBlt( timeBlt );
        BitBlt( hdc, 0, 0, rect.right, rect.bottom, hdcBack, 0, 0, SRCCOPY );
        GdiFlush();
    
        SelectObject( hdcBack, bmpOld );
        DeleteObject( bmpBack );
        DeleteObject( hdcBack );
    }
} //RenderToDC

void PutBitmapInClipboard( HWND hwnd, HBITMAP hbitmap )
{
    if ( OpenClipboard( hwnd ) )
    {
        EmptyClipboard();

        DIBSECTION ds;
        if ( sizeof ds == GetObject( hbitmap, sizeof ds, &ds ) )
        {
            HDC hdc = GetDC( HWND_DESKTOP );
            HBITMAP hdib = CreateDIBitmap( hdc, &ds.dsBmih, CBM_INIT, ds.dsBm.bmBits, (BITMAPINFO *) & ds.dsBmih, DIB_RGB_COLORS );
            ReleaseDC( NULL, hdc );

            HANDLE h = SetClipboardData( CF_BITMAP, hdib );

           // Windows owns hdib on success, but not failure

            if ( NULL == h )
                DeleteObject( hdib );
        }

        CloseClipboard();
    }
} //PutBitmapInClipboard

void PutBitmapInFile( HBITMAP hbitmap )
{
    CreateDirectory( g_imagesFolder, 0 );
    static WCHAR awcFile[ 100 ];
    const int MaxFile = 1000000;
    int i = 0;

    do
    {
        swprintf_s( awcFile, _countof( awcFile ), L"%ws\\osc-%d.png", g_imagesFolder, i );
        if ( INVALID_FILE_ATTRIBUTES == GetFileAttributes( awcFile ) )
            break;
        i++;
    } while ( i < MaxFile );

    if ( i < MaxFile )
    {
        Bitmap bmp( hbitmap, 0 );
        CLSID clsidPNG;
        CLSIDFromString( L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &clsidPNG );
        bmp.Save( awcFile, &clsidPNG );
    }
} //PutBitmapInFile

void RenderView( HWND hwnd, bool clipboard )
{
    RECT rect;
    GetClientRect( hwnd, &rect );

    HDC hdcClip = CreateCompatibleDC( NULL );
    void *pvBits;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof BITMAPINFOHEADER;
    bmi.bmiHeader.biWidth = rect.right;
    bmi.bmiHeader.biHeight = rect.bottom;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;
    HBITMAP bmpClip = CreateDIBSection( hdcClip, &bmi, DIB_RGB_COLORS, &pvBits, 0, 0 );
    HBITMAP bmpOld = (HBITMAP) SelectObject( hdcClip, bmpClip );
    
    RenderToDC( hdcClip, rect );
    SelectObject( hdcClip, bmpOld );

    if ( clipboard )
        PutBitmapInClipboard( hwnd, bmpClip );
    else
        PutBitmapInFile( bmpClip );
    
    DeleteObject( bmpClip );
    DeleteDC( hdcClip );
} //RenderView

extern "C" INT_PTR WINAPI HelpDialogProc( HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam )
{
    static const WCHAR * helpText = L"usage:\n"
                                     "\tosc input [-i] [-o:n] [-p:n] [-r] [-t]\n"
                                     "\n"
                                     "arguments:\n"
                                     "\tinput\tThe uncompressed WAV file to display\n"
                                     "\t-i\tCreates PNGs in osc_images\\osc-N for each frame shown\n"
                                     "\t-I\tLike -i, but first deletes PNG files in osc_images\\*\n"
                                     "\t-o:n\tOffset; start at n seconds into the WAV file\n"
                                     "\t-p:n\tThe period, where n is A through G above middle C\n"
                                     "\t-r\tIgnore prior window position stored in the registry\n"
                                     "\t-t\tAppend debugging traces to osc.txt\n"
                                     "\t-T\tLike -t, bur first delete osc.txt\n"
                                     "\n"
                                     "mouse:\n"
                                     "\tleft-click \tmove the window\n"
                                     "\tright-click\tcontext menu\n"
                                     "\n"
                                     "keyboard:\n"
                                     "\tctrl+c\t\tcopy current view to the clipboard\n"
                                     "\tctrl+s\t\tsaves current view to osc_images\\osc-N.png\n"
                                     "\tPage Up  \tZoom out. Increase period by one half step\n"
                                     "\tPage Down\tZoom in. Decrease period by one half step\n"
                                     "\tUp Arrow\tIncrease amplitude\n"
                                     "\tDown Arrow\tDecrease amplitude\n"
                                     "\tRight Arrow\tShift right in the WAV file\n"
                                     "\tLeft Arrow\tShift left in the WAV file\n"
                                     "\tq or esc   \tquit the application\n"
                                     "\n"
                                     "sample usage:\n"
                                     "\tosc myfile.wav\n"
                                     "\tosc myfile.wav -o:30.2\n"
                                     "\tosc myfile.wav -p:f\n"
                                     "\tosc d:\\songs\\myfile.wav -T -p:g -o:0.5\n"
                                     "\n"
                                     "notes:\n"
                                     "\tOnly uncompressed WAV files are supported\n"
                                     "\tChannel 0 (left) is white. 1 is Red. Shared values are Blue.\n"
                                     "\tOnly the first 16 channels are displayed\n";

    switch( message )
    {
        case WM_INITDIALOG:
        {
            SetDlgItemText( hdlg, ID_OSC_HELP_DIALOG_TEXT, helpText );
            return true;
        }
        case WM_COMMAND:
        {
            if ( LOWORD( wParam ) == IDOK || LOWORD( wParam ) == IDCANCEL )
                EndDialog( hdlg, IDCANCEL );
            break;
        }
    }

    return 0;
} //HelpDialogProc

LRESULT CALLBACK WindowProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    static HMENU hContextMenu = 0;

    switch ( uMsg )
    {
        case WM_CREATE:
        {
            hContextMenu = LoadMenu( NULL, MAKEINTRESOURCE( ID_OSC_POPUPMENU ) );
            break;
        }

        case WM_DESTROY:
        {
            if ( 0 != hContextMenu )
                DestroyMenu( hContextMenu );

            RECT rectPos;
            GetWindowRect( hwnd, &rectPos );

            static WCHAR awcPos[ 100 ] = {};
            int len = swprintf_s( awcPos, _countof( awcPos ), L"%d %d", rectPos.left, rectPos.top );
            CDJLRegistry::writeStringToRegistry( HKEY_CURRENT_USER, REGISTRY_APP_NAME, REGISTRY_WINDOW_POSITION, awcPos );

            PostQuitMessage( 0 );
            return 0;
        }

        case WM_CHAR:
        {
            if ( 'q' == wParam || 0x1b == wParam ) // q or ESC
                DestroyWindow( hwnd );
            return 0;
        }

        case WM_CONTEXTMENU:
        {
            POINT pt = { (LONG) (short) LOWORD( lParam ), (LONG) (short) HIWORD( lParam ) };
            if ( -1 == pt.x && -1 == pt.y )
                GetCursorPos( &pt );

            TrackPopupMenu( GetSubMenu( hContextMenu, 0 ), TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL );
            break;
        }

        case WM_PAINT:
        {
            if ( g_createImages )
                RenderView( hwnd, false );

            RECT rect;
            GetClientRect( hwnd, &rect );

            PAINTSTRUCT ps;
            HDC hdc = BeginPaint( hwnd, &ps );
            RenderToDC( hdc, rect );
            EndPaint( hwnd, &ps );

            return 0;
        }

        case WM_NCHITTEST:
        {
            if ( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 )
            {
                // Turn the whole window into what Windows thinks is the title bar so the user can drag the window around

                if ( HTCLIENT == DefWindowProc( hwnd, uMsg, wParam, lParam ) )
                    return HTCAPTION;
                return HTNOWHERE;
            }

            break;
        }

        case WM_COMMAND:
        {
            if ( ID_OSC_COPY == wParam )
                RenderView( hwnd, true );
            else if ( ID_OSC_SAVE == wParam )
                RenderView( hwnd, false );
            else if ( ID_OSC_HELP == wParam )
            {
                HWND helpDialog = CreateDialog( NULL, MAKEINTRESOURCE( ID_OSC_HELP_DIALOG ), hwnd, HelpDialogProc );
                ShowWindow( helpDialog, SW_SHOW );
            }

            break;
        }

        case WM_KEYDOWN:
        {
            if ( 'S' == wParam && ( GetKeyState( VK_CONTROL ) & 0x8000 ) )
            {
                RenderView( hwnd, false );
            }
            else if ( VK_F1 == wParam )
            {
                HWND helpDialog = CreateDialog( NULL, MAKEINTRESOURCE( ID_OSC_HELP_DIALOG ), hwnd, HelpDialogProc );
                ShowWindow( helpDialog, SW_SHOW );
            }
            else if ( ( VK_LEFT == wParam ) && ( g_secondsOffset > 0.0 ) )
            {
                g_secondsOffset = __max( 0.0, g_secondsOffset - ( g_viewPeriod / 10.0 ) );
                InvalidateRect( hwnd, NULL, TRUE );
            }
            else if ( ( VK_RIGHT == wParam ) && ( g_secondsOffset < g_wavSeconds ) )
            {
                g_secondsOffset = __min( g_wavSeconds, g_secondsOffset + ( g_viewPeriod / 10.0 ) );
                InvalidateRect( hwnd, NULL, TRUE );
            }
            else if ( ( VK_UP == wParam ) && ( g_amplitudeZoom < 20.0 ) )
            {
                g_amplitudeZoom += 0.1;
                g_amplitudeZoom = __min( 20.0, g_amplitudeZoom );
                InvalidateRect( hwnd, NULL, TRUE );
            }
            else if ( ( VK_DOWN == wParam ) && ( g_amplitudeZoom > 0.1 ) )
            {
                g_amplitudeZoom -= 0.1;
                g_amplitudeZoom = __max( 0.1, g_amplitudeZoom );
                InvalidateRect( hwnd, NULL, TRUE );
            }
            else if ( ( VK_PRIOR == wParam ) && ( PeriodIndex() > -240 ) )
            {
               g_notePeriod--;
               UpdateCurrentPeriod();
               InvalidateRect( hwnd, NULL, TRUE );
            }
            else if ( ( VK_NEXT == wParam ) && ( PeriodIndex() < 124 ) )
            {
                g_notePeriod++;
                UpdateCurrentPeriod();
                InvalidateRect( hwnd, NULL, TRUE );
            }
            else if ( ( 0x43 == wParam ) && ( GetKeyState( VK_CONTROL ) & 0x8000 ) ) // ^c for copy
            {
                RenderView( hwnd, true );
            }
            
            break;
        }
    }

    return DefWindowProc( hwnd, uMsg, wParam, lParam );
} //WindowProc
