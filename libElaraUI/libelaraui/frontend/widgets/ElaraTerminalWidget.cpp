#include "ElaraTerminalWidget.h"
#include "ElaraRootWidget.h"

#include <cairo/cairo.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>

namespace elara {

// ---------------------------------------------------------------------------
// XTerm 256-color palette
// ---------------------------------------------------------------------------

static const uint8_t ANSI16_COLORS[16][3] = {
    {0,0,0},       {170,0,0},     {0,170,0},     {170,85,0},
    {0,0,170},     {170,0,170},   {0,170,170},   {170,170,170},
    {85,85,85},    {255,85,85},   {85,255,85},   {255,255,85},
    {85,85,255},   {255,85,255},  {85,255,255},  {255,255,255},
};

void ElaraTerminalWidget::palette256(int idx, double& r, double& g, double& b) {
    if (idx < 0) idx = 0;
    if (idx < 16) {
        r = ANSI16_COLORS[idx][0] / 255.0;
        g = ANSI16_COLORS[idx][1] / 255.0;
        b = ANSI16_COLORS[idx][2] / 255.0;
    } else if (idx < 232) {
        int i = idx - 16;
        int ri = i / 36;
        int gi = (i % 36) / 6;
        int bi = i % 6;
        r = ri ? (55.0 + ri * 40.0) / 255.0 : 0.0;
        g = gi ? (55.0 + gi * 40.0) / 255.0 : 0.0;
        b = bi ? (55.0 + bi * 40.0) / 255.0 : 0.0;
    } else if (idx < 256) {
        double l = (8.0 + (idx - 232) * 10.0) / 255.0;
        r = g = b = l;
    } else {
        r = g = b = 0.0;
    }
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

ElaraTerminalWidget::ElaraTerminalWidget(ElaraWidgetRegister* root, ElaraWidgetHandle handle)
    : ElaraWidget(root, handle),
      font_size(13.0),
      cell_w(0), cell_h(0), cell_ascent(0),
      cols(0), rows(0),
      cursor_col(0), cursor_row(0),
      cursor_col_saved(0), cursor_row_saved(0),
      cursor_visible(true), focused(false), app_cursor_keys(false),
      scroll_top(0), scroll_bottom(0),
      alt_screen(false),
      cells(0), cells_normal(0), cells_alt(0),
      cur_fg(DEFAULT_FG), cur_bg(DEFAULT_BG), cur_attrs(0),
      master_fd(-1), child_pid(-1),
      reader_running(false), thread_started(false),
      cairo_surface(0), surface_w(0), surface_h(0), rgba_buf(0),
      parser_state(PS_GROUND),
      csi_param_count(0),
      utf8_codepoint(0), utf8_remaining(0)
{
    memset(csi_params, 0, sizeof(csi_params));
    pthread_mutex_init(&grid_mutex, NULL);
}

ElaraTerminalWidget::~ElaraTerminalWidget() {
    reader_running = false;

    if (child_pid > 0) {
        kill(child_pid, SIGKILL);
        int status;
        waitpid(child_pid, &status, 0);
        child_pid = -1;
    }

    // Close master fd after child dies — causes reader's read() to return EIO
    if (master_fd >= 0) {
        close(master_fd);
        master_fd = -1;
    }

    if (thread_started) {
        pthread_join(reader_thread, NULL);
        thread_started = false;
    }

    pthread_mutex_destroy(&grid_mutex);

    free(cells_normal);
    free(cells_alt);
    cells_normal = cells_alt = cells = NULL;

    if (cairo_surface) {
        cairo_surface_destroy((cairo_surface_t*)cairo_surface);
        cairo_surface = NULL;
    }

    free(rgba_buf);
    rgba_buf = NULL;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ElaraTerminalWidget::spawn(const String& cwd) {
    pthread_mutex_lock(&grid_mutex);
    if (cols <= 0 || rows <= 0) {
        resizeGrid(80, 24);
    }
    pthread_mutex_unlock(&grid_mutex);
    spawnShell((const char*)cwd);
}

void ElaraTerminalWidget::sendInput(const void* data, int len) {
    if (master_fd >= 0 && data && len > 0) {
        write(master_fd, data, len);
    }
}

// ---------------------------------------------------------------------------
// Cell grid management
// ---------------------------------------------------------------------------

ElaraTerminalWidget::TermCell ElaraTerminalWidget::makeBlankCell() const {
    TermCell c;
    c.ch = 0;
    c.fg = cur_fg;
    c.bg = cur_bg;
    c.attrs = 0;
    return c;
}

void ElaraTerminalWidget::clearGrid(TermCell* grid, int count) {
    TermCell blank;
    blank.ch = 0;
    blank.fg = DEFAULT_FG;
    blank.bg = DEFAULT_BG;
    blank.attrs = 0;
    for (int i = 0; i < count; i++) grid[i] = blank;
}

void ElaraTerminalWidget::resizeGrid(int new_cols, int new_rows) {
    if (new_cols <= 0) new_cols = 1;
    if (new_rows <= 0) new_rows = 1;

    int new_size = new_cols * new_rows;
    TermCell* new_normal = (TermCell*)malloc(new_size * sizeof(TermCell));
    TermCell* new_alt    = (TermCell*)malloc(new_size * sizeof(TermCell));

    TermCell blank;
    blank.ch = 0; blank.fg = DEFAULT_FG; blank.bg = DEFAULT_BG; blank.attrs = 0;
    for (int i = 0; i < new_size; i++) { new_normal[i] = blank; new_alt[i] = blank; }

    // Copy existing content
    if (cells_normal && cols > 0 && rows > 0) {
        int copy_rows = rows < new_rows ? rows : new_rows;
        int copy_cols = cols < new_cols ? cols : new_cols;
        for (int r = 0; r < copy_rows; r++) {
            for (int c = 0; c < copy_cols; c++) {
                new_normal[r * new_cols + c] = cells_normal[r * cols + c];
            }
        }
    }

    free(cells_normal);
    free(cells_alt);
    cells_normal = new_normal;
    cells_alt    = new_alt;
    cells = alt_screen ? cells_alt : cells_normal;

    cols = new_cols;
    rows = new_rows;
    scroll_top = 0;
    scroll_bottom = rows - 1;

    if (cursor_col >= cols) cursor_col = cols - 1;
    if (cursor_row >= rows) cursor_row = rows - 1;
}

// ---------------------------------------------------------------------------
// Font measurement
// ---------------------------------------------------------------------------

void ElaraTerminalWidget::measureFont() {
    cairo_surface_t* tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100);
    cairo_t* cr = cairo_create(tmp);
    cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);

    cairo_font_extents_t fe;
    cairo_font_extents(cr, &fe);
    cell_h = fe.height;
    cell_ascent = fe.ascent;

    cairo_text_extents_t te;
    cairo_text_extents(cr, "M", &te);
    cell_w = te.x_advance;

    cairo_destroy(cr);
    cairo_surface_destroy(tmp);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void ElaraTerminalWidget::getCellColor(uint32_t color_val, bool is_fg, uint8_t attrs,
                                        double& r, double& g, double& b) const {
    if (color_val & 0x80000000u) {
        r = ((color_val >> 16) & 0xFF) / 255.0;
        g = ((color_val >> 8) & 0xFF) / 255.0;
        b = (color_val & 0xFF) / 255.0;
    } else {
        int idx = (int)(color_val & 0xFF);
        if (is_fg && (attrs & ATTR_BOLD) && idx < 8) idx += 8;
        palette256(idx, r, g, b);
    }
    if (is_fg && (attrs & ATTR_DIM)) {
        r *= 0.6; g *= 0.6; b *= 0.6;
    }
}

void ElaraTerminalWidget::renderSurface(void* cr_ptr) {
    cairo_t* cr = (cairo_t*)cr_ptr;

    // Default background fill
    double def_r, def_g, def_b;
    palette256((int)DEFAULT_BG, def_r, def_g, def_b);
    cairo_set_source_rgb(cr, def_r, def_g, def_b);
    cairo_paint(cr);

    if (!cells || cols <= 0 || rows <= 0 || cell_w <= 0 || cell_h <= 0) return;

    char utf8buf[8];

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            TermCell& cell = cells[row * cols + col];

            uint32_t fg = cell.fg;
            uint32_t bg = cell.bg;
            uint8_t attrs = cell.attrs;

            if (attrs & ATTR_REVERSE) { uint32_t tmp = fg; fg = bg; bg = tmp; }

            bool is_cursor = (row == cursor_row && col == cursor_col && cursor_visible);

            double bkr, bkg, bkb;
            if (is_cursor && focused) {
                bkr = 1.0; bkg = 1.0; bkb = 1.0;
            } else {
                getCellColor(bg, false, attrs, bkr, bkg, bkb);
            }

            // Only fill if different from default
            bool bg_differs = is_cursor ||
                (fabs(bkr - def_r) > 0.005 ||
                 fabs(bkg - def_g) > 0.005 ||
                 fabs(bkb - def_b) > 0.005);

            if (bg_differs) {
                cairo_set_source_rgb(cr, bkr, bkg, bkb);
                cairo_rectangle(cr, col * cell_w, row * cell_h, cell_w, cell_h);
                cairo_fill(cr);
            }

            // Text
            uint32_t ch = cell.ch;
            if (!ch || ch == ' ' || (attrs & ATTR_INVISIBLE)) continue;

            if (attrs & ATTR_BOLD) {
                cairo_select_font_face(cr, "Monospace",
                    CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            } else {
                cairo_select_font_face(cr, "Monospace",
                    CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            }
            cairo_set_font_size(cr, font_size);

            double fr, fg2, fb;
            getCellColor(fg, true, attrs, fr, fg2, fb);
            if (is_cursor && focused) {
                fr = 1.0 - fr; fg2 = 1.0 - fg2; fb = 1.0 - fb;
            }
            cairo_set_source_rgb(cr, fr, fg2, fb);

            // Codepoint → UTF-8
            int utf8len = 0;
            if (ch < 0x80) {
                utf8buf[utf8len++] = (char)ch;
            } else if (ch < 0x800) {
                utf8buf[utf8len++] = (char)(0xC0 | (ch >> 6));
                utf8buf[utf8len++] = (char)(0x80 | (ch & 0x3F));
            } else if (ch < 0x10000) {
                utf8buf[utf8len++] = (char)(0xE0 | (ch >> 12));
                utf8buf[utf8len++] = (char)(0x80 | ((ch >> 6) & 0x3F));
                utf8buf[utf8len++] = (char)(0x80 | (ch & 0x3F));
            } else {
                utf8buf[utf8len++] = (char)(0xF0 | (ch >> 18));
                utf8buf[utf8len++] = (char)(0x80 | ((ch >> 12) & 0x3F));
                utf8buf[utf8len++] = (char)(0x80 | ((ch >> 6) & 0x3F));
                utf8buf[utf8len++] = (char)(0x80 | (ch & 0x3F));
            }
            utf8buf[utf8len] = '\0';

            cairo_move_to(cr, col * cell_w, row * cell_h + cell_ascent);
            cairo_show_text(cr, utf8buf);

            if (attrs & ATTR_UNDERLINE) {
                cairo_set_line_width(cr, 1.0);
                cairo_move_to(cr, col * cell_w, row * cell_h + cell_ascent + 2.0);
                cairo_rel_line_to(cr, cell_w, 0.0);
                cairo_stroke(cr);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// draw()
// ---------------------------------------------------------------------------

void ElaraTerminalWidget::draw(ElaraDrawContext* ctx) {
    pthread_mutex_lock(&grid_mutex);

    if (cell_w <= 0) measureFont();

    if (cell_w > 0 && cell_h > 0) {
        int new_cols = (int)(width / cell_w);
        int new_rows = (int)(height / cell_h);
        if (new_cols < 1) new_cols = 1;
        if (new_rows < 1) new_rows = 1;

        if (new_cols != cols || new_rows != rows) {
            resizeGrid(new_cols, new_rows);
            if (master_fd >= 0) {
                struct winsize ws;
                ws.ws_col = (unsigned short)cols;
                ws.ws_row = (unsigned short)rows;
                ws.ws_xpixel = 0;
                ws.ws_ypixel = 0;
                ioctl(master_fd, TIOCSWINSZ, &ws);
                if (child_pid > 0) kill(child_pid, SIGWINCH);
            }
        }
    }

    int sw = (int)width;
    int sh = (int)height;
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;

    if (!cairo_surface || surface_w != sw || surface_h != sh) {
        if (cairo_surface)
            cairo_surface_destroy((cairo_surface_t*)cairo_surface);
        cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
        surface_w = sw;
        surface_h = sh;
        free(rgba_buf);
        rgba_buf = (uint8_t*)malloc((size_t)sw * (size_t)sh * 4);
    }

    if (cairo_surface && rgba_buf) {
        cairo_t* cr = cairo_create((cairo_surface_t*)cairo_surface);
        renderSurface(cr);
        cairo_destroy(cr);
    }

    pthread_mutex_unlock(&grid_mutex);

    if (!cairo_surface || !rgba_buf) return;

    cairo_surface_flush((cairo_surface_t*)cairo_surface);
    const uint8_t* src = cairo_image_surface_get_data((cairo_surface_t*)cairo_surface);
    int cstride = cairo_image_surface_get_stride((cairo_surface_t*)cairo_surface);

    for (int row = 0; row < sh; row++) {
        const uint8_t* srow = src + (size_t)row * (size_t)cstride;
        uint8_t* drow = rgba_buf + (size_t)row * (size_t)sw * 4;
        for (int col = 0; col < sw; col++) {
            // Cairo ARGB32 memory: [B,G,R,A] → drawBitmapRgba wants [R,G,B,A]
            drow[col*4+0] = srow[col*4+2]; // R
            drow[col*4+1] = srow[col*4+1]; // G
            drow[col*4+2] = srow[col*4+0]; // B
            drow[col*4+3] = srow[col*4+3]; // A
        }
    }

    ctx->drawBitmapRgba(0, 0, sw, sh, rgba_buf, sw * 4);
}

// ---------------------------------------------------------------------------
// setBounds
// ---------------------------------------------------------------------------

void ElaraTerminalWidget::setBounds(double px, double py, double w, double h) {
    ElaraWidget::setBounds(px, py, w, h);
}

// ---------------------------------------------------------------------------
// setFocused
// ---------------------------------------------------------------------------

void ElaraTerminalWidget::setFocused(bool f) {
    focused = f;
    ElaraRootWidget* root = rootWidget();
    if (root) root->getGuiBackend()->invalidate();
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

void ElaraTerminalWidget::onMouseDown(int button, double px, double py) {
    (void)button; (void)px; (void)py;
    ElaraRootWidget* root = rootWidget();
    if (root) root->setFocus(widget_handle);
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

const char* ElaraTerminalWidget::keyvalToEscape(unsigned int kv) const {
    if (app_cursor_keys) {
        if (kv == 0xFF52) return "\x1bOA";
        if (kv == 0xFF54) return "\x1bOB";
        if (kv == 0xFF53) return "\x1bOC";
        if (kv == 0xFF51) return "\x1bOD";
    } else {
        if (kv == 0xFF52) return "\x1b[A";
        if (kv == 0xFF54) return "\x1b[B";
        if (kv == 0xFF53) return "\x1b[C";
        if (kv == 0xFF51) return "\x1b[D";
    }
    switch (kv) {
        case 0xFF50: return app_cursor_keys ? "\x1bOH" : "\x1b[1~";
        case 0xFF57: return app_cursor_keys ? "\x1bOF" : "\x1b[4~";
        case 0xFF55: return "\x1b[5~";
        case 0xFF56: return "\x1b[6~";
        case 0xFFFF: return "\x1b[3~";
        case 0xFF08: return "\x7f";
        case 0xFF09: return "\x09";
        case 0xFF0D: return "\x0d";
        case 0xFF8D: return "\x0d";
        case 0xFF1B: return "\x1b";
        case 0xFFBE: return "\x1bOP";
        case 0xFFBF: return "\x1bOQ";
        case 0xFFC0: return "\x1bOR";
        case 0xFFC1: return "\x1bOS";
        case 0xFFC2: return "\x1b[15~";
        case 0xFFC3: return "\x1b[17~";
        case 0xFFC4: return "\x1b[18~";
        case 0xFFC5: return "\x1b[19~";
        case 0xFFC6: return "\x1b[20~";
        case 0xFFC7: return "\x1b[21~";
        case 0xFFC8: return "\x1b[23~";
        case 0xFFC9: return "\x1b[24~";
        default: return NULL;
    }
}

void ElaraTerminalWidget::onKeyDown(unsigned int keyval, unsigned int modifiers) {
    if (master_fd < 0) return;

    const char* seq = keyvalToEscape(keyval);
    if (seq) {
        write(master_fd, seq, strlen(seq));
        return;
    }

    // Ctrl+letter → control character (Ctrl+C = 0x03, Ctrl+Z = 0x1A, etc.)
    if ((modifiers & ELARA_KEY_MOD_CTRL) && keyval >= 0x40 && keyval <= 0x7F) {
        char ctrl = (char)(keyval & 0x1F);
        write(master_fd, &ctrl, 1);
        return;
    }

    // Alt/Meta prefix: send ESC before the character
    bool alt = (modifiers & ELARA_KEY_MOD_ALT) != 0;

    // Printable ASCII
    if (keyval >= 0x20 && keyval <= 0x7E) {
        char ch = (char)keyval;
        if (alt) { char esc = '\x1b'; write(master_fd, &esc, 1); }
        write(master_fd, &ch, 1);
        return;
    }

    // Non-ASCII Unicode: encode to UTF-8
    if (keyval > 0x7E && keyval < 0xFF00) {
        char utf8[5];
        int len = 0;
        if (keyval < 0x800) {
            utf8[len++] = (char)(0xC0 | (keyval >> 6));
            utf8[len++] = (char)(0x80 | (keyval & 0x3F));
        } else if (keyval < 0x10000) {
            utf8[len++] = (char)(0xE0 | (keyval >> 12));
            utf8[len++] = (char)(0x80 | ((keyval >> 6) & 0x3F));
            utf8[len++] = (char)(0x80 | (keyval & 0x3F));
        } else {
            utf8[len++] = (char)(0xF0 | (keyval >> 18));
            utf8[len++] = (char)(0x80 | ((keyval >> 12) & 0x3F));
            utf8[len++] = (char)(0x80 | ((keyval >> 6) & 0x3F));
            utf8[len++] = (char)(0x80 | (keyval & 0x3F));
        }
        if (len > 0) {
            if (alt) { char esc = '\x1b'; write(master_fd, &esc, 1); }
            write(master_fd, utf8, len);
        }
    }
}

void ElaraTerminalWidget::onKeysTyped(const String& text) {
    // onKeysTyped is not dispatched by the GTK backend; onKeyDown handles all input.
    // This is a fallback for RPC-dispatched text (ui.typeWidget).
    if (master_fd < 0) return;
    const char* s = (const char*)text;
    if (s && *s) write(master_fd, s, strlen(s));
}

void ElaraTerminalWidget::onKeyDown(unsigned int keyval) {
    onKeyDown(keyval, 0);
}

// ---------------------------------------------------------------------------
// PTY + reader thread
// ---------------------------------------------------------------------------

ElaraRootWidget* ElaraTerminalWidget::rootWidget() const {
    ElaraWidget* cur = (ElaraWidget*)this;
    while (cur) {
        ElaraRootWidget* root = dynamic_cast<ElaraRootWidget*>(cur);
        if (root) return root;
        cur = cur->getParent();
    }
    return NULL;
}

bool ElaraTerminalWidget::spawnShell(const char* cwd) {
    int mfd = posix_openpt(O_RDWR | O_NOCTTY);
    if (mfd < 0) return false;
    if (grantpt(mfd) < 0 || unlockpt(mfd) < 0) { close(mfd); return false; }

    char* slave_name = ptsname(mfd);
    if (!slave_name) { close(mfd); return false; }

    // Make a copy before fork (ptsname returns static buffer)
    char slave_path[256];
    strncpy(slave_path, slave_name, sizeof(slave_path) - 1);
    slave_path[sizeof(slave_path) - 1] = '\0';

    struct winsize ws;
    ws.ws_col  = (unsigned short)(cols > 0 ? cols : 80);
    ws.ws_row  = (unsigned short)(rows > 0 ? rows : 24);
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    ioctl(mfd, TIOCSWINSZ, &ws);

    pid_t pid = fork();
    if (pid < 0) { close(mfd); return false; }

    if (pid == 0) {
        // Child
        close(mfd);
        setsid();

        int sfd = open(slave_path, O_RDWR);
        if (sfd < 0) _exit(1);

        ioctl(sfd, TIOCSCTTY, 0);
        dup2(sfd, STDIN_FILENO);
        dup2(sfd, STDOUT_FILENO);
        dup2(sfd, STDERR_FILENO);
        if (sfd > 2) close(sfd);

        setenv("TERM", "xterm-256color", 1);
        char cols_buf[16], rows_buf[16];
        snprintf(cols_buf, sizeof(cols_buf), "%d", (int)ws.ws_col);
        snprintf(rows_buf, sizeof(rows_buf), "%d", (int)ws.ws_row);
        setenv("COLUMNS", cols_buf, 1);
        setenv("LINES",   rows_buf, 1);

        if (cwd && cwd[0]) chdir(cwd);

        const char* shell = getenv("SHELL");
        if (!shell || !shell[0]) shell = "/bin/bash";
        execl(shell, shell, "-l", (char*)NULL);
        execl("/bin/bash", "/bin/bash", "-l", (char*)NULL);
        _exit(1);
    }

    // Parent
    master_fd = mfd;
    child_pid = pid;

    reader_running = true;
    if (pthread_create(&reader_thread, NULL, readerThreadEntry, this) != 0) {
        reader_running = false;
        close(mfd);
        master_fd = -1;
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        child_pid = -1;
        return false;
    }
    thread_started = true;
    return true;
}

void* ElaraTerminalWidget::readerThreadEntry(void* arg) {
    ((ElaraTerminalWidget*)arg)->readerLoop();
    return NULL;
}

void ElaraTerminalWidget::readerLoop() {
    int fd = master_fd;
    uint8_t buf[4096];

    while (reader_running) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;

        pthread_mutex_lock(&grid_mutex);
        for (ssize_t i = 0; i < n; i++) feedByte(buf[i]);
        pthread_mutex_unlock(&grid_mutex);

        ElaraRootWidget* root = rootWidget();
        if (root) root->getGuiBackend()->invalidate();
    }
}

// ---------------------------------------------------------------------------
// UTF-8 decoder
// ---------------------------------------------------------------------------

void ElaraTerminalWidget::feedByte(uint8_t byte) {
    if (byte < 0x80) {
        utf8_codepoint = 0;
        utf8_remaining = 0;
        feedCodepoint((uint32_t)byte);
    } else if ((byte & 0xE0) == 0xC0) {
        utf8_codepoint = byte & 0x1F;
        utf8_remaining = 1;
    } else if ((byte & 0xF0) == 0xE0) {
        utf8_codepoint = byte & 0x0F;
        utf8_remaining = 2;
    } else if ((byte & 0xF8) == 0xF0) {
        utf8_codepoint = byte & 0x07;
        utf8_remaining = 3;
    } else if ((byte & 0xC0) == 0x80) {
        if (utf8_remaining > 0) {
            utf8_codepoint = (utf8_codepoint << 6) | (byte & 0x3F);
            utf8_remaining--;
            if (utf8_remaining == 0) {
                feedCodepoint(utf8_codepoint);
                utf8_codepoint = 0;
            }
        }
    } else {
        utf8_remaining = 0;
        utf8_codepoint = 0;
    }
}

// ---------------------------------------------------------------------------
// ANSI parser dispatch
// ---------------------------------------------------------------------------

void ElaraTerminalWidget::feedCodepoint(uint32_t cp) {
    switch (parser_state) {
        case PS_GROUND:
            if (cp == 0x1B) {
                parser_state = PS_ESCAPE;
            } else if (cp < 0x20 || cp == 0x7F) {
                processControl(cp);
            } else {
                processPrintable(cp);
            }
            break;
        case PS_ESCAPE:
            processEscape(cp);
            break;
        case PS_CSI:
            processCSI(cp);
            break;
        case PS_CSI_PRIV:
            processCSIPriv(cp);
            break;
        case PS_OSC:
            processOSC(cp);
            break;
        case PS_SS3:
            processSS3(cp);
            break;
        case PS_DCS:
            if (cp == 0x1B) parser_state = PS_ESCAPE;
            break;
    }
}

void ElaraTerminalWidget::processControl(uint32_t cp) {
    switch (cp) {
        case 0x07: break; // BEL
        case 0x08: // BS
            if (cursor_col > 0) cursor_col--;
            break;
        case 0x09: // HT
            cursor_col = ((cursor_col / 8) + 1) * 8;
            if (cursor_col >= cols) cursor_col = cols - 1;
            break;
        case 0x0A: // LF
        case 0x0B: // VT
        case 0x0C: // FF
            if (cursor_row == scroll_bottom) {
                scrollUp(1);
            } else if (cursor_row < rows - 1) {
                cursor_row++;
            }
            break;
        case 0x0D: // CR
            cursor_col = 0;
            break;
        default: break;
    }
}

void ElaraTerminalWidget::processPrintable(uint32_t cp) {
    if (!cells || cols <= 0 || rows <= 0) return;

    if (cursor_col >= cols) {
        cursor_col = 0;
        if (cursor_row == scroll_bottom) {
            scrollUp(1);
        } else if (cursor_row < rows - 1) {
            cursor_row++;
        }
    }

    TermCell& cell = cells[cursor_row * cols + cursor_col];
    cell.ch    = cp;
    cell.fg    = cur_fg;
    cell.bg    = cur_bg;
    cell.attrs = cur_attrs;
    cursor_col++;
}

void ElaraTerminalWidget::processEscape(uint32_t cp) {
    parser_state = PS_GROUND;
    switch (cp) {
        case '[':
            parser_state = PS_CSI;
            memset(csi_params, 0, sizeof(csi_params));
            csi_param_count = 0;
            break;
        case ']':
            parser_state = PS_OSC;
            break;
        case 'O':
            parser_state = PS_SS3;
            break;
        case 'P': case 'X': case '^': case '_':
            parser_state = PS_DCS;
            break;
        case '7': // DECSC
            cursor_col_saved = cursor_col;
            cursor_row_saved = cursor_row;
            break;
        case '8': // DECRC
            cursor_col = cursor_col_saved;
            cursor_row = cursor_row_saved;
            if (cursor_col >= cols) cursor_col = cols - 1;
            if (cursor_row >= rows) cursor_row = rows - 1;
            break;
        case 'M': // Reverse index
            if (cursor_row == scroll_top) {
                scrollDown(1);
            } else if (cursor_row > 0) {
                cursor_row--;
            }
            break;
        case 'c': // RIS - full reset
            cur_fg = DEFAULT_FG; cur_bg = DEFAULT_BG; cur_attrs = 0;
            cursor_col = 0; cursor_row = 0;
            scroll_top = 0; scroll_bottom = rows - 1;
            app_cursor_keys = false;
            if (cells) clearGrid(cells, cols * rows);
            break;
        case '=': case '>': // Application/normal keypad mode
            break;
        case '\\': break; // ST
        default: break;
    }
}

void ElaraTerminalWidget::processCSI(uint32_t cp) {
    if (cp == '?') {
        parser_state = PS_CSI_PRIV;
        return;
    }
    if (cp >= '0' && cp <= '9') {
        if (csi_param_count == 0) csi_param_count = 1;
        csi_params[csi_param_count - 1] =
            csi_params[csi_param_count - 1] * 10 + (int)(cp - '0');
        return;
    }
    if (cp == ';') {
        if (csi_param_count < 15) csi_param_count++;
        csi_params[csi_param_count - 1] = 0;
        return;
    }

    parser_state = PS_GROUND;
    int p0 = csi_param_count > 0 ? csi_params[0] : 0;
    int p1 = csi_param_count > 1 ? csi_params[1] : 0;

    switch (cp) {
        case 'A': // CUU
            cursor_row -= (p0 ? p0 : 1);
            if (cursor_row < scroll_top) cursor_row = scroll_top;
            break;
        case 'B': // CUD
            cursor_row += (p0 ? p0 : 1);
            if (cursor_row > scroll_bottom) cursor_row = scroll_bottom;
            break;
        case 'C': // CUF
            cursor_col += (p0 ? p0 : 1);
            if (cursor_col >= cols) cursor_col = cols - 1;
            break;
        case 'D': // CUB
            cursor_col -= (p0 ? p0 : 1);
            if (cursor_col < 0) cursor_col = 0;
            break;
        case 'E': // CNL
            cursor_row += (p0 ? p0 : 1);
            cursor_col = 0;
            if (cursor_row >= rows) cursor_row = rows - 1;
            break;
        case 'F': // CPL
            cursor_row -= (p0 ? p0 : 1);
            cursor_col = 0;
            if (cursor_row < 0) cursor_row = 0;
            break;
        case 'G': // CHA
            cursor_col = (p0 ? p0 : 1) - 1;
            if (cursor_col < 0) cursor_col = 0;
            if (cursor_col >= cols) cursor_col = cols - 1;
            break;
        case 'H': case 'f': // CUP / HVP
            cursor_row = (p0 ? p0 : 1) - 1;
            cursor_col = (p1 ? p1 : 1) - 1;
            if (cursor_row < 0) cursor_row = 0;
            if (cursor_row >= rows) cursor_row = rows - 1;
            if (cursor_col < 0) cursor_col = 0;
            if (cursor_col >= cols) cursor_col = cols - 1;
            break;
        case 'J': eraseDisplay(p0); break;
        case 'K': eraseLine(p0); break;
        case 'L': insertLines(p0 ? p0 : 1); break;
        case 'M': deleteLines(p0 ? p0 : 1); break;
        case 'P': deleteChars(p0 ? p0 : 1); break;
        case 'S': scrollUp(p0 ? p0 : 1); break;
        case 'T': scrollDown(p0 ? p0 : 1); break;
        case 'd': // VPA
            cursor_row = (p0 ? p0 : 1) - 1;
            if (cursor_row < 0) cursor_row = 0;
            if (cursor_row >= rows) cursor_row = rows - 1;
            break;
        case 'm': applySGR(); break;
        case 'r': // DECSTBM
            scroll_top    = (p0 ? p0 : 1) - 1;
            scroll_bottom = (p1 ? p1 : rows) - 1;
            if (scroll_top < 0) scroll_top = 0;
            if (scroll_bottom >= rows) scroll_bottom = rows - 1;
            if (scroll_top >= scroll_bottom) {
                scroll_top = 0; scroll_bottom = rows - 1;
            }
            cursor_col = 0; cursor_row = 0;
            break;
        case 'h': case 'l': break; // Standard mode set/reset (ignored)
        case 'n': // DSR — ignore for now
        case 'c': // DA — ignore
        default: break;
    }
}

void ElaraTerminalWidget::processCSIPriv(uint32_t cp) {
    if (cp >= '0' && cp <= '9') {
        if (csi_param_count == 0) csi_param_count = 1;
        csi_params[csi_param_count - 1] =
            csi_params[csi_param_count - 1] * 10 + (int)(cp - '0');
        return;
    }
    if (cp == ';') {
        if (csi_param_count < 15) csi_param_count++;
        csi_params[csi_param_count - 1] = 0;
        return;
    }

    parser_state = PS_GROUND;
    bool set = (cp == 'h');
    if (!set && cp != 'l') return;

    for (int i = 0; i < (csi_param_count > 0 ? csi_param_count : 1); i++) {
        int p = csi_params[i];
        switch (p) {
            case 1: app_cursor_keys = set; break;
            case 25: cursor_visible = set; break;
            case 47: case 1047:
                if (set && !alt_screen) {
                    clearGrid(cells_alt, cols * rows);
                    cells = cells_alt;
                    cursor_col = cursor_row = 0;
                    alt_screen = true;
                } else if (!set && alt_screen) {
                    cells = cells_normal;
                    alt_screen = false;
                }
                break;
            case 1049:
                if (set && !alt_screen) {
                    cursor_col_saved = cursor_col;
                    cursor_row_saved = cursor_row;
                    clearGrid(cells_alt, cols * rows);
                    cells = cells_alt;
                    cursor_col = cursor_row = 0;
                    alt_screen = true;
                } else if (!set && alt_screen) {
                    cells = cells_normal;
                    cursor_col = cursor_col_saved;
                    cursor_row = cursor_row_saved;
                    if (cursor_col >= cols) cursor_col = cols - 1;
                    if (cursor_row >= rows) cursor_row = rows - 1;
                    alt_screen = false;
                }
                break;
            default: break;
        }
    }
}

void ElaraTerminalWidget::processOSC(uint32_t cp) {
    if (cp == 0x07 || cp == 0x9C) { // BEL or ST
        parser_state = PS_GROUND;
    } else if (cp == 0x1B) {
        parser_state = PS_ESCAPE; // Will see '\' next
    }
    // Ignore OSC content (title changes, etc.)
}

void ElaraTerminalWidget::processSS3(uint32_t cp) {
    parser_state = PS_GROUND;
    // Application keypad / cursor sequences
    switch (cp) {
        case 'A': case 'B': case 'C': case 'D': {
            // Cursor keys in SS3 mode
            char seq[3] = {'\x1b', 'O', (char)cp};
            // Already handled — if we're here, just process as cursor movement
            break;
        }
        case 'P': case 'Q': case 'R': case 'S':
            break; // F1-F4 in application mode — ignore (keydown handles input)
        default: break;
    }
}

// ---------------------------------------------------------------------------
// SGR
// ---------------------------------------------------------------------------

void ElaraTerminalWidget::applySGR() {
    if (csi_param_count == 0) {
        cur_fg = DEFAULT_FG; cur_bg = DEFAULT_BG; cur_attrs = 0;
        return;
    }

    for (int i = 0; i < csi_param_count; ) {
        int p = csi_params[i];
        switch (p) {
            case 0:
                cur_fg = DEFAULT_FG; cur_bg = DEFAULT_BG; cur_attrs = 0;
                i++; break;
            case 1: cur_attrs |= ATTR_BOLD;      i++; break;
            case 2: cur_attrs |= ATTR_DIM;       i++; break;
            case 3: cur_attrs |= ATTR_ITALIC;    i++; break;
            case 4: cur_attrs |= ATTR_UNDERLINE; i++; break;
            case 5: cur_attrs |= ATTR_BLINK;     i++; break;
            case 7: cur_attrs |= ATTR_REVERSE;   i++; break;
            case 8: cur_attrs |= ATTR_INVISIBLE; i++; break;
            case 22: cur_attrs &= ~(uint8_t)(ATTR_BOLD | ATTR_DIM); i++; break;
            case 23: cur_attrs &= ~(uint8_t)ATTR_ITALIC;    i++; break;
            case 24: cur_attrs &= ~(uint8_t)ATTR_UNDERLINE; i++; break;
            case 25: cur_attrs &= ~(uint8_t)ATTR_BLINK;     i++; break;
            case 27: cur_attrs &= ~(uint8_t)ATTR_REVERSE;   i++; break;
            case 28: cur_attrs &= ~(uint8_t)ATTR_INVISIBLE; i++; break;
            case 39: cur_fg = DEFAULT_FG; i++; break;
            case 49: cur_bg = DEFAULT_BG; i++; break;
            default:
                if (p >= 30 && p <= 37) { cur_fg = (uint32_t)(p - 30); i++; }
                else if (p >= 40 && p <= 47) { cur_bg = (uint32_t)(p - 40); i++; }
                else if (p >= 90 && p <= 97) { cur_fg = (uint32_t)(p - 90 + 8); i++; }
                else if (p >= 100 && p <= 107) { cur_bg = (uint32_t)(p - 100 + 8); i++; }
                else if ((p == 38 || p == 48) && i + 1 < csi_param_count) {
                    bool is_fg = (p == 38);
                    int sub = csi_params[i + 1];
                    if (sub == 5 && i + 2 < csi_param_count) {
                        uint32_t idx = (uint32_t)(csi_params[i + 2] & 0xFF);
                        if (is_fg) cur_fg = idx; else cur_bg = idx;
                        i += 3;
                    } else if (sub == 2 && i + 4 < csi_param_count) {
                        uint32_t r = (uint32_t)(csi_params[i + 2] & 0xFF);
                        uint32_t g = (uint32_t)(csi_params[i + 3] & 0xFF);
                        uint32_t b = (uint32_t)(csi_params[i + 4] & 0xFF);
                        uint32_t rgb = 0x80000000u | (r << 16) | (g << 8) | b;
                        if (is_fg) cur_fg = rgb; else cur_bg = rgb;
                        i += 5;
                    } else { i++; }
                } else { i++; }
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// Screen operations
// ---------------------------------------------------------------------------

void ElaraTerminalWidget::scrollUp(int n) {
    if (!cells || n <= 0) return;
    for (int i = 0; i < n; i++) {
        for (int row = scroll_top; row < scroll_bottom; row++)
            memcpy(&cells[row * cols], &cells[(row + 1) * cols],
                   (size_t)cols * sizeof(TermCell));
        TermCell blank = {0, DEFAULT_FG, DEFAULT_BG, 0};
        for (int col = 0; col < cols; col++) cells[scroll_bottom * cols + col] = blank;
    }
}

void ElaraTerminalWidget::scrollDown(int n) {
    if (!cells || n <= 0) return;
    for (int i = 0; i < n; i++) {
        for (int row = scroll_bottom; row > scroll_top; row--)
            memcpy(&cells[row * cols], &cells[(row - 1) * cols],
                   (size_t)cols * sizeof(TermCell));
        TermCell blank = {0, DEFAULT_FG, DEFAULT_BG, 0};
        for (int col = 0; col < cols; col++) cells[scroll_top * cols + col] = blank;
    }
}

void ElaraTerminalWidget::eraseDisplay(int mode) {
    if (!cells) return;
    TermCell blank = {0, DEFAULT_FG, DEFAULT_BG, 0};
    if (mode == 0) {
        for (int col = cursor_col; col < cols; col++)
            cells[cursor_row * cols + col] = blank;
        for (int row = cursor_row + 1; row < rows; row++)
            for (int col = 0; col < cols; col++) cells[row * cols + col] = blank;
    } else if (mode == 1) {
        for (int row = 0; row < cursor_row; row++)
            for (int col = 0; col < cols; col++) cells[row * cols + col] = blank;
        for (int col = 0; col <= cursor_col && col < cols; col++)
            cells[cursor_row * cols + col] = blank;
    } else if (mode == 2 || mode == 3) {
        clearGrid(cells, cols * rows);
        if (mode == 2) { cursor_col = 0; cursor_row = 0; }
    }
}

void ElaraTerminalWidget::eraseLine(int mode) {
    if (!cells) return;
    TermCell blank = {0, DEFAULT_FG, DEFAULT_BG, 0};
    if (mode == 0) {
        for (int col = cursor_col; col < cols; col++) cells[cursor_row * cols + col] = blank;
    } else if (mode == 1) {
        for (int col = 0; col <= cursor_col && col < cols; col++) cells[cursor_row * cols + col] = blank;
    } else if (mode == 2) {
        for (int col = 0; col < cols; col++) cells[cursor_row * cols + col] = blank;
    }
}

void ElaraTerminalWidget::insertLines(int n) {
    if (!cells || n <= 0 || cursor_row < scroll_top || cursor_row > scroll_bottom) return;
    TermCell blank = {0, DEFAULT_FG, DEFAULT_BG, 0};
    for (int i = 0; i < n; i++) {
        for (int row = scroll_bottom; row > cursor_row; row--)
            memcpy(&cells[row * cols], &cells[(row - 1) * cols], (size_t)cols * sizeof(TermCell));
        for (int col = 0; col < cols; col++) cells[cursor_row * cols + col] = blank;
    }
}

void ElaraTerminalWidget::deleteLines(int n) {
    if (!cells || n <= 0) return;
    TermCell blank = {0, DEFAULT_FG, DEFAULT_BG, 0};
    for (int i = 0; i < n; i++) {
        for (int row = cursor_row; row < scroll_bottom; row++)
            memcpy(&cells[row * cols], &cells[(row + 1) * cols], (size_t)cols * sizeof(TermCell));
        for (int col = 0; col < cols; col++) cells[scroll_bottom * cols + col] = blank;
    }
}

void ElaraTerminalWidget::deleteChars(int n) {
    if (!cells || n <= 0) return;
    TermCell blank = {0, DEFAULT_FG, DEFAULT_BG, 0};
    int from = cursor_col + n;
    if (from > cols) from = cols;
    int count = cols - from;
    if (count > 0)
        memmove(&cells[cursor_row * cols + cursor_col],
                &cells[cursor_row * cols + from],
                (size_t)count * sizeof(TermCell));
    for (int col = cursor_col + count; col < cols; col++)
        cells[cursor_row * cols + col] = blank;
}

} // namespace elara
