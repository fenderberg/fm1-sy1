/* Optional green LED/LCD faces. Original 5x7 glyphs and seven-segment digits, drawn into
 * the existing canvas strips. No framebuffer, font heap or audio-side changes. */
#pragma once
#define UX_GREEN (set.display != DISPLAY_NORD)
#define UX_LCD (set.display == DISPLAY_LCD)
#define UX_BG (UX_LCD ? RGB(166, 193, 89) : UX_GREEN ? RGB(1, 7, 3) : RGB(17, 19, 20))
#define UX_PANEL (UX_LCD ? RGB(158, 184, 82) : UX_GREEN ? RGB(3, 15, 7) : RGB(28, 31, 32))
#define UX_RULE (UX_LCD ? RGB(138, 164, 73) : UX_GREEN ? RGB(17, 57, 26) : RGB(57, 61, 62))
#define UX_TEXT (UX_LCD ? RGB(15, 33, 16) : UX_GREEN ? RGB(115, 245, 126) : RGB(244, 239, 223))
#define UX_MUTED (UX_LCD ? RGB(49, 70, 30) : UX_GREEN ? RGB(55, 143, 71) : RGB(160, 162, 157))
#define UX_RED (UX_LCD ? RGB(145, 173, 73) : UX_GREEN ? RGB(8, 35, 14) : RGB(195, 43, 35))
#define UX_AMBER (UX_LCD ? RGB(8, 24, 12) : UX_GREEN ? RGB(182, 255, 150) : RGB(255, 199, 100))
#define UX_FOCUS (UX_LCD ? RGB(186, 211, 110) : UX_GREEN ? RGB(7, 35, 13) : RGB(51, 43, 29))

/* Seven rows per glyph, five bits per row. Lowercase displays as uppercase only;
 * patch bytes and screen-reader text retain their original spelling. */
static const uint8_t ux_pixel_letters[36][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
};
static uint8_t ux_pixel_row(unsigned char c, uint32_t row)
{
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (c >= 'A' && c <= 'Z') return ux_pixel_letters[c-'A'][row];
    if (c >= '0' && c <= '9') return ux_pixel_letters[26+c-'0'][row];
    switch(c) {
    case ' ': return 0;
    case '.': return row==6 ? 4 : 0;
    case ',': return row==5 ? 4 : row==6 ? 8 : 0;
    case ':': return row==2 || row==5 ? 4 : 0;
    case ';': return row==2 || row==5 ? 4 : row==6 ? 8 : 0;
    case '-': return row==3 ? 14 : 0;
    case '_': return row==6 ? 31 : 0;
    case '/': return 1u << (row*4/6);
    case '\\': return 16u >> (row*4/6);
    case '+': return row==3 ? 14 : row>=1 && row<=5 ? 4 : 0;
    case '#': return row==2 || row==4 ? 31 : 10;
    case '%': return row==0 || row==1 ? 25 : row==5 || row==6 ? 19 : 1u<<(row*4/6);
    case '>': return row<4 ? 16u>>row : 16u>>(6-row);
    case '<': return row<4 ? 1u<<row : 1u<<(6-row);
    case '(': case '[': return row==0 || row==6 ? 6 : 8;
    case ')': case ']': return row==0 || row==6 ? 12 : 2;
    case '=': return row==2 || row==4 ? 14 : 0;
    case '!': return row<4 || row==6 ? 4 : 0;
    case '*': return row==2 || row==4 ? 10 : row==3 ? 4 : 0;
    case '\'': return row<2 ? 4 : 0;
    case '"': return row<2 ? 10 : 0;
    default: return row==0 ? 14 : row==1 ? 17 : row==2 ? 2 : row==3 || row==6 ? 4 : 0;
    }
}
static int ux_led_number(const felucca_font_t *font, const char *s)
{
    int digit=0;
    if (font != &FONT_M && font != &FONT_S) return 0;
    for (;*s;s++) {
        if (*s>='0' && *s<='9') digit=1;
        else if (*s!='-' && *s!='+' && *s!='.' && *s!='/') return 0;
    }
    return digit;
}
static int32_t ux_display_width(const felucca_font_t *font, const char *s)
{
    int32_t width=0;
    const int large=font==&FONT_M;
    if (!UX_GREEN) return text_w(font,s);
    const int segments=ux_led_number(font,s);
    for(;*s;s++) width+=segments ? (*s=='.' ? 4 : large ? 15 : 10) : large || font==&FONT_S ? 12 : 6;
    return width ? width-(segments ? 2 : large || font==&FONT_S ? 2 : 1) : 0;
}
static void ux_segment_digit(int32_t x, int32_t y, char c, int large, uint16_t color)
{
    static const uint8_t digits[]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    const int32_t w=large?12:8,h=large?22:15,t=large?2:1,mid=h/2;
    const uint8_t mask=c>='0'&&c<='9'?digits[c-'0']:c=='-'?0x40:0;
    const int32_t segments[7][4]={{t,0,w-2*t,t},{w-t,t,t,mid-t},{w-t,mid+t,t,h-mid-2*t},
        {t,h-t,w-2*t,t},{0,mid+t,t,h-mid-2*t},{0,t,t,mid-t},{t,mid,w-2*t,t}};
    if(c=='.') {cv_rect(x,y+h-t,t,t,color);return;}
    if(c=='/') {cv_line(x,y+h-1,x+w-1,y,color);return;}
    if(c=='+') {cv_rect(x, y+mid,w,t,color);cv_rect(x+w/2,y+h/4,t,h/2,color);return;}
    for(uint32_t i=0;i<7;i++) {
        const int32_t *r=segments[i];
        cv_rect(x+r[0],y+r[1],r[2],r[3],mask&(1u<<i)?color:UX_RULE);
    }
}
static int32_t ux_display_text(int32_t x, int32_t y, const felucca_font_t *font, const char *s, uint16_t color)
{
    if (!UX_GREEN) return cv_text(x,y,font,s,color);
#ifdef SY1_TEST_PIXEL_TEXT
    SY1_TEST_PIXEL_TEXT(s);
#endif
    const int large=font==&FONT_M,scale=large||font==&FONT_S?2:1;
    const int segments=ux_led_number(font,s);
    const int32_t end=x+ux_display_width(font,s);
    for(;*s;s++) {
        if(segments) {ux_segment_digit(x,y,*s,large,color);x+=*s=='.'?4:large?15:10;}
        else {
            for(uint32_t r=0;r<7;r++) {
                const uint8_t bits=ux_pixel_row((unsigned char)*s,r);
                for(uint32_t c=0;c<5;c++) if(bits&(16u>>c)) cv_rect(x+(int32_t)c*scale,y+(int32_t)r*scale,scale,scale,color);
            }
            x+=6*scale;
        }
    }
    return end;
}
