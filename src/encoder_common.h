
// popcnt table, but with +8 since it's what we do in the encoder
static const unsigned char BitsSetTable256plus8[256] = 
{
#   define B2(n) n+8,     n+9,     n+9,     n+10
#   define B4(n) B2(n), B2(n+1), B2(n+1), B2(n+2)
#   define B6(n) B4(n), B4(n+1), B4(n+1), B4(n+2)
    B6(0), B6(1), B6(1), B6(2)
#undef B2
#undef B4
#undef B6
};
