#ifndef ELARA_TERMINAL_WIDGET_H
#define ELARA_TERMINAL_WIDGET_H

#include "ElaraWidget.h"
#include <pthread.h>
#include <sys/types.h>
#include <stdint.h>

namespace elara {

class ElaraRootWidget;

class ElaraTerminalWidget : public ElaraWidget {
public:
    static const uint32_t DEFAULT_FG = 7;
    static const uint32_t DEFAULT_BG = 0;

    struct TermCell {
        uint32_t ch;     // Unicode codepoint (0 = space)
        uint32_t fg;     // 0x80RRGGBB = truecolor, else palette index
        uint32_t bg;
        uint8_t  attrs;
    };

    enum AttrFlag {
        ATTR_BOLD      = 0x01,
        ATTR_DIM       = 0x02,
        ATTR_ITALIC    = 0x04,
        ATTR_UNDERLINE = 0x08,
        ATTR_BLINK     = 0x10,
        ATTR_REVERSE   = 0x20,
        ATTR_INVISIBLE = 0x40,
    };

    ElaraTerminalWidget(ElaraWidgetRegister* root, ElaraWidgetHandle handle);
    virtual ~ElaraTerminalWidget();

    void spawn(const String& cwd);
    void sendInput(const void* data, int len);

    virtual void draw(ElaraDrawContext* ctx);
    virtual void setBounds(double px, double py, double w, double h);
    virtual void setFocused(bool f);
    virtual void onKeyDown(unsigned int keyval);
    virtual void onKeyDown(unsigned int keyval, unsigned int modifiers);
    virtual void onKeysTyped(const String& text);
    virtual void onMouseDown(int button, double px, double py);

private:
    // Font metrics
    double font_size;
    double cell_w;
    double cell_h;
    double cell_ascent;

    // Grid
    int cols;
    int rows;
    int cursor_col;
    int cursor_row;
    int cursor_col_saved;
    int cursor_row_saved;
    bool cursor_visible;
    bool focused;
    bool app_cursor_keys;

    // Scroll region
    int scroll_top;
    int scroll_bottom;

    // Alternate screen
    bool alt_screen;
    TermCell* cells;
    TermCell* cells_normal;
    TermCell* cells_alt;

    // Current SGR state
    uint32_t cur_fg;
    uint32_t cur_bg;
    uint8_t  cur_attrs;

    // PTY
    int master_fd;
    pid_t child_pid;

    // Reader thread
    pthread_t reader_thread;
    bool reader_running;
    bool thread_started;

    // Mutex
    pthread_mutex_t grid_mutex;

    // Cairo offscreen surface (opaque pointers to avoid pulling in cairo.h)
    void* cairo_surface;
    int surface_w;
    int surface_h;
    uint8_t* rgba_buf;

    // ANSI parser
    enum ParserState {
        PS_GROUND,
        PS_ESCAPE,
        PS_CSI,
        PS_CSI_PRIV,
        PS_OSC,
        PS_SS3,
        PS_DCS,
    };

    ParserState parser_state;
    int csi_params[16];
    int csi_param_count;

    // UTF-8 decoder
    uint32_t utf8_codepoint;
    int utf8_remaining;

    // Internal
    ElaraRootWidget* rootWidget() const;
    void measureFont();
    void resizeGrid(int new_cols, int new_rows);
    void clearGrid(TermCell* grid, int count);
    TermCell makeBlankCell() const;

    void renderSurface(void* cr_ptr);
    void getCellColor(uint32_t color_val, bool is_fg, uint8_t attrs,
                      double& r, double& g, double& b) const;
    static void palette256(int idx, double& r, double& g, double& b);

    bool spawnShell(const char* cwd);
    static void* readerThreadEntry(void* arg);
    void readerLoop();

    void feedByte(uint8_t byte);
    void feedCodepoint(uint32_t cp);
    void processControl(uint32_t cp);
    void processPrintable(uint32_t cp);
    void processEscape(uint32_t cp);
    void processCSI(uint32_t cp);
    void processCSIPriv(uint32_t cp);
    void processOSC(uint32_t cp);
    void processSS3(uint32_t cp);
    void applySGR();

    void scrollUp(int n);
    void scrollDown(int n);
    void eraseDisplay(int mode);
    void eraseLine(int mode);
    void insertLines(int n);
    void deleteLines(int n);
    void deleteChars(int n);

    const char* keyvalToEscape(unsigned int keyval) const;
};

} // namespace elara
#endif
