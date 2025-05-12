```c
/**
 * @brief  Tell whether a timestamp is in the future relative to a current tick
 *         value, handling 32‑bit wrap‑around.
 *
 * The test treats the 32‑bit counter as an unsigned integer that wraps
 * modulo 2³².  A point ≤ 2³¹ ticks *ahead* of @p current_time is considered
 * “in the future”; everything else (the present, the past, or ≥ 2³¹ ticks
 * away) is “not in the future”.  This makes comparisons unambiguous across
 * the wrap point.
 *
 * @param current_time  The clock value “now”.
 * @param target_time   The time we want to test.
 *
 * @retval true   @p target_time is in the future.
 * @retval false  @p target_time is now or in the past (or ≥ 2³¹ ticks ahead).
 */
static inline bool
is_in_the_future(uint32_t current_time, uint32_t target_time)
{
    //  Unsigned subtraction is defined modulo 2³².
    //  If the distance forward is < 2³¹, the result is < 0x80000000.
    return (uint32_t)(target_time - current_time) < 0x80000000u;
}
```

Conceptually

```c
return (int32_t)(target_time - current_time) > 0;
```

is the same algorithm, but it relies on converting an out‑of‑range unsigned 
value to signed. The C standard says that conversion is implementation‑defined 
and may even trap on some exotic (non‑two’s complement) platforms. On the vast 
majority of modern embedded compilers it does work, yet the purely‑unsigned 
version above avoids that portability risk and makes the intent clearer.

Bottom line: use the unsigned‑arithmetic form; it is terse, portable, and 
provably correct for any monotonically increasing 32‑bit tick counter that wraps
at 2³².
