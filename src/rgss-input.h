/*
 * RGSS3 Input semantics, ported from mkxp-z src/input/input.cpp (InputPrivate), without SDL:
 * the platform only supplies a bit mask of physical buttons per update and a binding table
 * (physical bit -> RGSS button code). Header-only so the host tests use the exact same code.
 *
 * Preserved from upstream:
 *  - per-RGSS-button pressed / triggered / released, computed once per Input.update;
 *  - a single repeating button: the first binding (table order) that became pressed this update
 *    and is not the current repeating one; repeated on its trigger frame, then when
 *    count >= repeatStart && (count + 1) % repeatDelay == 0 (RGSS3 at 60 fps: 23 and 6);
 *  - dir4 with "previous direction" priority and dead combos (up+down, left+right) -> 0;
 *  - dir8 combos table.
 */
#ifndef RGSS_INPUT_H
#define RGSS_INPUT_H

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace RgssInput
{

enum Code
{
    None = 0,
    Down = 2, Left = 4, Right = 6, Up = 8,
    A = 11, B = 12, C = 13, X = 14, Y = 15, Z = 16, L = 17, R = 18,
    Shift = 21, Ctrl = 22, Alt = 23,
    F5 = 25, F6 = 26, F7 = 27, F8 = 28, F9 = 29,
    CodeCount = 32
};

struct Binding
{
    uint32_t source;   /* one physical button bit */
    int target;        /* RGSS button code */
};

struct State
{
    bool pressed, triggered, repeated, released;
};

class Input
{
public:
    explicit Input(unsigned int fps = 60) { setFps(fps); std::memset(states_, 0, sizeof(states_)); std::memset(old_, 0, sizeof(old_)); }

    void setBindings(const std::vector<Binding> &b) { bindings_ = b; }

    /* As InputPrivate::recalcRepeatTime for rgssVer >= 2. */
    void setFps(unsigned int fps)
    {
        if (fps == 0)
            return;
        const double framems = 1.0 / fps;
        repeatStart_ = (unsigned int)std::ceil(0.375 / framems);
        repeatDelay_ = (unsigned int)std::ceil(0.100 / framems);
    }

    void update(uint32_t physical)
    {
        std::memcpy(old_, states_, sizeof(states_));
        std::memset(states_, 0, sizeof(states_));

        int repeatCand = None;
        for (const Binding &b : bindings_) {
            if (!(physical & b.source) || b.target <= None || b.target >= CodeCount)
                continue;
            State &st = states_[b.target];
            const State &os = old_[b.target];
            st.pressed = true;
            if (!os.pressed)
                st.triggered = true;
            if (repeatCand != None)
                continue;
            if (repeating_ != b.target && !os.pressed)
                repeatCand = b.target;       /* every Vita button is repeatable */
        }
        for (int c = 1; c < CodeCount; ++c)
            if (!states_[c].pressed && old_[c].pressed)
                states_[c].released = true;

        updateDir4();
        updateDir8();

        if (repeatCand != None && repeatCand != repeating_) {
            repeating_ = repeatCand;
            repeatCount_ = 0;
            states_[repeatCand].repeated = true;
            return;
        }
        if (repeating_ != None && states_[repeating_].pressed) {
            ++repeatCount_;
            const bool rep = repeatCount_ >= repeatStart_ && ((repeatCount_ + 1) % repeatDelay_) == 0;
            states_[repeating_].repeated |= rep;
            return;
        }
        repeating_ = None;
    }

    const State &state(int code) const
    {
        static const State none = { false, false, false, false };
        return (code > None && code < CodeCount) ? states_[code] : none;
    }
    bool isPressed(int code) const { return state(code).pressed; }
    bool isTriggered(int code) const { return state(code).triggered; }
    bool isRepeated(int code) const { return state(code).repeated; }
    bool isReleased(int code) const { return state(code).released; }
    int dir4() const { return dir4Active_; }
    int dir8() const { return dir8Active_; }

private:
    void updateDir4()
    {
        static const int dirs[4] = { Down, Left, Right, Up };
        static const int dirFlags[4] = { 1 << Down, 1 << Left, 1 << Right, 1 << Up };
        static const int deadDirFlags[2] = { (1 << Down) | (1 << Up), (1 << Left) | (1 << Right) };
        static const int otherDirs[4][3] = {
            { Left, Right, Up }, { Down, Up, Right }, { Down, Up, Left }, { Left, Right, Down } };

        int dirFlag = 0;
        for (int i = 0; i < 4; ++i)
            dirFlag |= states_[dirs[i]].pressed ? dirFlags[i] : 0;

        if (dirFlag == deadDirFlags[0] || dirFlag == deadDirFlags[1]) {
            dir4Active_ = None;
            return;
        }
        if (dir4Previous_ != None && states_[dir4Previous_].pressed) {
            for (int i = 0; i < 3; ++i) {
                const int other = otherDirs[(dir4Previous_ / 2) - 1][i];
                if (!states_[other].pressed)
                    continue;
                dir4Active_ = other;
                return;
            }
        }
        for (int i = 0; i < 4; ++i) {
            if (!states_[dirs[i]].pressed)
                continue;
            dir4Active_ = dirs[i];
            dir4Previous_ = dirs[i];
            return;
        }
        dir4Active_ = None;
        dir4Previous_ = None;
    }

    void updateDir8()
    {
        static const int dirs[4] = { Down, Left, Right, Up };
        static const int otherDirs[4][3] = {
            { Left, Right, Up }, { Down, Up, Right }, { Down, Up, Left }, { Left, Right, Down } };
        static const int combos[4][4] = {
            { 2, 1, 3, 0 }, { 1, 4, 0, 7 }, { 3, 0, 6, 9 }, { 0, 7, 9, 8 } };

        dir8Active_ = 0;
        for (int i = 0; i < 4; ++i) {
            const int one = dirs[i];
            if (!states_[one].pressed)
                continue;
            for (int j = 0; j < 3; ++j) {
                const int other = otherDirs[i][j];
                if (!states_[other].pressed)
                    continue;
                dir8Active_ = combos[(one / 2) - 1][(other / 2) - 1];
                return;
            }
            dir8Active_ = one;
            return;
        }
    }

    std::vector<Binding> bindings_;
    State states_[CodeCount];
    State old_[CodeCount];
    int repeating_ = None;
    unsigned int repeatCount_ = 0;
    unsigned int repeatStart_ = 23, repeatDelay_ = 6;
    int dir4Active_ = None, dir4Previous_ = None;
    int dir8Active_ = 0;
};

/* RGSS3 key argument: Symbol name or Integer code (Input::DOWN, :DOWN, 2 ...). */
static inline int codeForName(const char *name)
{
    static const struct { const char *n; int c; } names[] = {
        { "DOWN", Down }, { "LEFT", Left }, { "RIGHT", Right }, { "UP", Up },
        { "A", A }, { "B", B }, { "C", C }, { "X", X }, { "Y", Y }, { "Z", Z }, { "L", L }, { "R", R },
        { "SHIFT", Shift }, { "CTRL", Ctrl }, { "ALT", Alt },
        { "F5", F5 }, { "F6", F6 }, { "F7", F7 }, { "F8", F8 }, { "F9", F9 },
    };
    for (const auto &e : names)
        if (!std::strcmp(e.n, name))
            return e.c;
    return None;
}

} // namespace RgssInput

#endif /* RGSS_INPUT_H */
