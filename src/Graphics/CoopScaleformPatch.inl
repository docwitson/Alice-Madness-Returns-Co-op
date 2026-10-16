// Runtime patch of the user's own Scaleform movie. No extracted game assets.
namespace CoopScaleformPatch
{
#include "CoopScaleformActions.inl"
    using Bytes = std::vector<uint8_t>;
    struct Edit { size_t offset; Bytes bytes; bool jumpToLayout = false; };
    inline std::vector<std::pair<uint32_t, uint32_t>> patchedActions;
    static uint16_t U16(const uint8_t* p) { uint16_t v; memcpy(&v, p, 2); return v; }
    static uint32_t U32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
    static void Put16(Bytes& b, size_t p, uint16_t v) { memcpy(b.data() + p, &v, 2); }
    static void Put32(Bytes& b, size_t p, uint32_t v) { memcpy(b.data() + p, &v, 4); }
    template<size_t N> static Bytes Copy(const uint8_t (&b)[N]) { return Bytes(b, b + N); }

    static Bytes RewriteBody(const uint8_t* data, size_t size, std::vector<Edit> edits)
    {
        std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.offset < b.offset; });
        auto address = [&](size_t old) { size_t n = old; for (const auto& e : edits) if (e.offset < old) n += e.bytes.size(); return n; };
        std::unordered_set<size_t> boundaries;
        for (size_t p = 0; p < size;)
        {
            boundaries.insert(p);
            const uint8_t op = data[p++];
            if (op >= 0x80) { if (p + 2 > size) return {}; size_t n = U16(data + p); p += 2; if (p + n > size) return {}; p += n; }
        }
        boundaries.insert(size);
        for (const auto& e : edits) if (!boundaries.count(e.offset)) return {};
        Bytes out;
        for (size_t p = 0; p <= size;)
        {
            for (const auto& e : edits) if (e.offset == p)
            {
                size_t start = out.size(); out.insert(out.end(), e.bytes.begin(), e.bytes.end());
                if (e.jumpToLayout)
                {
                    // Original CreateMainMenu: section = "menu" at action+0xD43.
                    intptr_t target = address(0xD43 - 0x9C8);
                    intptr_t delta = target - static_cast<intptr_t>(out.size());
                    if (delta < INT16_MIN || delta > INT16_MAX || out.size() < start + 5) return {};
                    Put16(out, out.size() - 2, static_cast<uint16_t>(delta));
                }
            }
            if (p == size) break;
            size_t start = p; uint8_t op = data[p++]; size_t n = 0;
            if (op >= 0x80) { n = U16(data + p); p += 2; }
            size_t end = p + n, dest = out.size();
            out.insert(out.end(), data + start, data + end);
            if (op == 0x99 || op == 0x9D)
            {
                if (n != 2) return {};
                intptr_t target = static_cast<intptr_t>(end) + static_cast<int16_t>(U16(data + p));
                if (target < 0 || target > static_cast<intptr_t>(size) || !boundaries.count(static_cast<size_t>(target))) return {};
                intptr_t delta = address(static_cast<size_t>(target)) - static_cast<intptr_t>(out.size());
                if (delta < INT16_MIN || delta > INT16_MAX) return {};
                Put16(out, dest + 3, static_cast<uint16_t>(delta));
            }
            p = end;
        }
        return out;
    }

    static Bytes RewriteActions(const uint8_t* data, size_t size, int& changed)
    {
        Bytes original(data, data + size);
        if (size < 4) return original;
        uint32_t sig = U32(data);
        // Apply upstream patches at their ORIGINAL offsets before relocating code.
        ApplyMenuBytePatches(original.data(), static_cast<uint32_t>(size), sig);
        if (!HideAlice1WhenMissing && sig == 0xA409AC88 && size > 0xC39 && original[0xC39] == 5) original[0xC39] = 0xD7;
        Bytes out;
        bool own = false;
        for (size_t p = 0; p < size;)
        {
            size_t start = p; uint8_t op = original[p++]; size_t n = 0;
            if (op >= 0x80) { if (p + 2 > size) return {}; n = U16(original.data() + p); p += 2; }
            if (p + n > size) return {};
            const uint8_t* payload = original.data() + p;
            if (op == 0x8E || op == 0x9B)
            {
                size_t z = 0; while (z < n && payload[z]) ++z;
                if (z + 3 > n) return {};
                std::string name(reinterpret_cast<const char*>(payload), z);
                uint16_t count = U16(payload + z + 1); size_t h = z + 3 + (op == 0x8E ? 3 : 0);
                for (uint16_t i = 0; i < count; ++i)
                {
                    if (op == 0x8E) ++h;
                    while (h < n && payload[h]) ++h; ++h;
                }
                if (h + 2 != n) return {};
                size_t bodySize = U16(payload + h), body = p + n;
                if (body + bodySize > size) return {};
                std::vector<Edit> edits;
                if (name == "CreateMainMenu" && sig == 0xA409AC88)
                {
                    if (body != 0x9C8 || bodySize != 1502 || original[0xA28] != 0x96 || original[0xDB6] != 0x96) return {};
                    edits = {{0, Copy(ClearInfo)}, {0xA28 - body, Copy(ChooseSubmenu), true},
                        {0xD12 - body, Copy(AppendMultiplayer)}, {bodySize, Copy(CreateInfo)}};
                }
                else if (name == "doEnterEvent" && sig == 0xA2078988 && bodySize == 352)
                    edits = {{0, Copy(EnterPrefix)}};
                else if (name == "closeMenu" && sig == 0xA2078988 && bodySize == 236)
                    edits = {{0, Copy(StageLeave)}};
                else if (name == "doBEvent" && sig == 0xA2078988 && bodySize == 1992)
                    edits = {{0, Copy(BackPrefix)}};
                else if ((name == "doXEvent" && bodySize == 2477) || (name == "doYEvent" && bodySize == 1494))
                {
                    if (sig == 0xA2078988) edits = {{0, Copy(IgnoreOtherShortcut)}};
                }
                if (!edits.empty())
                {
                    Bytes fixed = RewriteBody(original.data() + body, bodySize, edits);
                    if (fixed.empty() || fixed.size() > UINT16_MAX) return {};
                    size_t dest = out.size(); out.insert(out.end(), original.begin() + start, original.begin() + body);
                    Put16(out, dest + (p - start) + h, static_cast<uint16_t>(fixed.size()));
                    out.insert(out.end(), fixed.begin(), fixed.end()); own = true; ++changed;
                }
                else out.insert(out.end(), original.begin() + start, original.begin() + body + bodySize);
                p = body + bodySize;
                continue;
            }
            if (op == 0 && own && sig == 0xA409AC88) out.insert(out.end(), std::begin(Register), std::end(Register));
            if (op == 0 && own && sig == 0xA2078988) out.insert(out.end(), std::begin(StageEnter), std::end(StageEnter));
            out.insert(out.end(), original.begin() + start, original.begin() + p + n); p += n;
        }
        if (own)
        {
            auto identity = std::make_pair(sig, static_cast<uint32_t>(out.size()));
            if (std::find(patchedActions.begin(), patchedActions.end(), identity) == patchedActions.end()) patchedActions.push_back(identity);
        }
        return out;
    }

    static Bytes RewriteTags(const uint8_t* data, size_t size, uint16_t sprite, int& changed)
    {
        Bytes out;
        unsigned frame = 0;
        for (size_t p = 0; p < size;)
        {
            if (p + 2 > size) return {};
            uint16_t head = U16(data + p), code = head >> 6; size_t h = 2, n = head & 63;
            if (n == 63) { if (p + 6 > size) return {}; n = U32(data + p + 2); h = 6; }
            if (n > size - p - h) return {};
            const uint8_t* body = data + p + h; Bytes fixed(body, body + n);
            if (code == 39 && n >= 4)
            {
                Bytes tags = RewriteTags(body + 4, n - 4, U16(body), changed);
                if (tags.empty()) return {};
                fixed.assign(body, body + 4); fixed.insert(fixed.end(), tags.begin(), tags.end());
            }
            else if (code == 12 && sprite == 295)
            {
                if (frame == 29 && n == 1003)
                { fixed = RewriteBody(body, n, {{0, Copy(StageLeave)}}); ++changed; }
                else fixed = RewriteActions(body, n, changed);
                if (fixed.empty()) return {};
            }
            // Native startup/back-to-title enters profile at gotoAndPlay(2),
            // so restore visibility on frame TWO, not initialization frame one.
            if (code == 1 && ((sprite == 134 && frame == 1) || (sprite == 70 && frame == 149) || (sprite == 295 && frame == 1)))
            {
                Bytes action = sprite == 134 ? Copy(ProfileEnter) : sprite == 70 ? Copy(TitleEnter) : Copy(StageEntrance); action.push_back(0);
                size_t tag = out.size(); out.resize(tag + 6);
                Put16(out, tag, static_cast<uint16_t>((12 << 6) | 63));
                Put32(out, tag + 2, static_cast<uint32_t>(action.size()));
                out.insert(out.end(), action.begin(), action.end()); ++changed;
            }
            if (code == 1) ++frame;
            uint16_t newHead = static_cast<uint16_t>((code << 6) | (h == 6 || fixed.size() >= 63 ? 63 : fixed.size()));
            size_t at = out.size(); out.resize(at + 2); Put16(out, at, newHead);
            if (h == 6 || fixed.size() >= 63) { at = out.size(); out.resize(at + 4); Put32(out, at, static_cast<uint32_t>(fixed.size())); }
            out.insert(out.end(), fixed.begin(), fixed.end()); p += h + n;
        }
        return out;
    }
    static bool IsPatchedAction(const uint8_t* data, uint32_t size)
    {
        if (!data || size < 4) return false;
        return std::find(patchedActions.begin(), patchedActions.end(), std::make_pair(U32(data), size)) != patchedActions.end();
    }
    static Bytes Build(const uint8_t* blob, uint32_t size)
    {
        if (size != 152316 || blob[3] != 8 || U32(blob + 4) != size) return {};
        size_t start = 8 + (5 + (blob[8] >> 3) * 4 + 7) / 8 + 4;
        if (start >= size) return {};
        int changed = 0; size_t previous = patchedActions.size();
        Bytes tags = RewriteTags(blob + start, size - start, 0, changed);
        if (tags.empty() || changed != 10) { patchedActions.resize(previous); return {}; }
        Bytes out(blob, blob + start); out.insert(out.end(), tags.begin(), tags.end());
        Put32(out, 4, static_cast<uint32_t>(out.size())); return out;
    }
}
