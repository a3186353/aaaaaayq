-- 在项目根目录用CI产物执行：GGELUA/lua.exe ggelua3_push/tests/test_jy_depth.lua [GGELUA目录]
-- 全部夹具在内存生成，不启动窗口、不下载资源、不写缓存；旧DLL缺入口时必须失败。
local engine = arg[1] or 'GGELUA'
gge = assert(package.loadlib(engine .. '/ggelua.dll', 'luaopen_ggelua'))()
package.loaded.ggelua = gge
package.path = engine .. '/ggelua/?.lua;' .. package.path
package.cpath = engine .. '/lib/?.dll;' .. package.cpath
local SDL = require('SDL.SDL')
local jy, depth = require('mygxy.jy'), require('mygxy.jy_depth')
local zlib = require('zlib')
local pack, char, concat = string.pack, string.char, table.concat
local signature = '\137PNG\13\10\26\10'
local assertions = 0
local function check(ok, message)
    assertions = assertions + 1
    assert(ok, message)
end
local function chunk(kind, raw)
    return pack('>I4', #raw) .. kind .. raw .. pack('>I4', zlib.crc32(kind .. raw))
end
local function png(raw, width, height, opts)
    opts = opts or {}
    local header = pack('>I4I4BBBBB', width, height, opts.depth or 16, opts.color or 0,
        opts.compression or 0, opts.filter or 0, opts.interlace or 0)
    local compressed = opts.compressed or zlib.compress(raw)
    local parts = {signature, chunk('IHDR', header), opts.before or ''}
    local step = opts.split or #compressed
    for at = 1, #compressed, step do parts[#parts + 1] = chunk('IDAT', compressed:sub(at, at + step - 1)) end
    parts[#parts + 1] = opts.after or ''
    parts[#parts + 1] = chunk('IEND', '')
    return concat(parts)
end
local function spr(image, opts)
    opts = opts or {}
    local frames = opts.frames or {{opts.image or 0, opts.x or 0, opts.y or 0,
        opts.w or 2, opts.h or 2, opts.kx or 17, opts.ky or -9}}
    local parts = {pack('<c2I2I2I2I2i2i2I2', 'RP', opts.groups or 1, #frames,
        opts.width or 256, opts.height or 256, 123, 456, opts.images or 1)}
    for _, f in ipairs(frames) do parts[#parts + 1] = pack('<i2i2i2I2I2i2i2', table.unpack(f)) end
    parts[#parts + 1] = pack('<I4i4i4', opts.typ or 1, 16 + #frames * 14 + 12, #image)
    parts[#parts + 1] = image
    local raw = concat(parts)
    return 'FTEN' .. pack('<I4I4I4', 1, #raw, 0) .. raw
end
local function replace(raw, at, value)
    return raw:sub(1, at - 1) .. value .. raw:sub(at + #value)
end
local function filtered(mode)
    local rows, previous = {}, {}
    for y = 0, 255 do
        local decoded = {}
        for x = 0, 255 do decoded[#decoded + 1] = y; decoded[#decoded + 1] = x end
        local row = {char(mode)}
        for i, value in ipairs(decoded) do
            local a, b, c = decoded[i - 2] or 0, previous[i] or 0, previous[i - 2] or 0
            local prediction = 0
            if mode == 1 then prediction = a
            elseif mode == 2 then prediction = b
            elseif mode == 3 then prediction = math.floor((a + b) / 2)
            elseif mode == 4 then
                local p = a + b - c
                local da, db, dc = math.abs(p - a), math.abs(p - b), math.abs(p - c)
                prediction = da <= db and da <= dc and a or (db <= dc and b or c)
            end
            row[#row + 1] = char((value - prediction) % 256)
        end
        rows[#rows + 1] = concat(row)
        previous = decoded
    end
    return concat(rows)
end
local function pixels(sf)
    local ptr, pitch = sf:LockSurface()
    local rw = assert(SDL.RWFromMem(ptr, pitch * sf.h))
    local data = rw:RWread(pitch * sf.h)
    rw:RWclose()
    sf:UnlockSurface()
    return data, pitch
end

-- 为全部65536个灰度建立独立的旧Atlas参考，不调用新的灰16解码逻辑。
local index, alpha, mask_index = {}, {}, {}
for y = 0, 255 do
    local a, b, c = {'\0'}, {'\0'}, {'\0'}
    for x = 0, 255 do
        a[#a + 1] = char(255, y, x)
        b[#b + 1] = x == 0 and y == 0 and '\0' or '\255'
        c[#c + 1] = char(0, y, x)
    end
    index[#index + 1], alpha[#alpha + 1], mask_index[#mask_index + 1] = concat(a), concat(b), concat(c)
end
local frame_table = {{sx = 0, sy = 0, sw = 256, sh = 256, key_x = 17, key_y = -9, z = 0}}
local body = jy(png(concat(index), 256, 256, {depth = 8, color = 2}), nil, nil, frame_table)
local reference = jy(png(concat(mask_index), 256, 256, {depth = 8, color = 2}),
    png(concat(alpha), 256, 256, {depth = 8}), nil, frame_table)
local target = assert(SDL.CreateRGBSurfaceWithFormat(256, 256, 32, 372645892))
local function compose(mask, bias)
    check(body:CompositeTo(target, 256, 256, {
        {body, 0, bias, 17, -9, 0, 0}, {mask, 0, 0, 17, -9, 0, 1},
    }) == true, 'CompositeTo失败')
    return pixels(target)
end
for mode = 0, 4 do
    local raw = spr(png(filtered(mode), 256, 256, {
        before = chunk('gAMA', pack('>I4', 100000)) .. chunk('cHRM', string.rep('\0', 32)),
        split = 127,
    }), {w = 256, h = 256})
    local started = os.clock()
    local mask, info = depth(raw)
    check(mask ~= nil, info)
    print(string.format('灰16滤波%d，原生读入 %.3f ms', mode, (os.clock() - started) * 1000))
    check(info.group == 1 and info.frame == 1 and info.total == 1 and info.frameRate == 8, '全局帧信息错误')
    check(info.width == 256 and info.height == 256 and info.x == 0 and info.y == 0, '旧Atlas全局几何改变')
    local fi = mask:GetFrameInfo(0)
    check(fi.x == 17 and fi.y == -9 and fi.width == 256 and fi.height == 256 and fi.z == 0, '帧几何改变')
    for bias = -1, 1 do
        local expected = compose(reference, bias)
        local actual, pitch = compose(mask, bias)
        check(actual == expected, '新旧完整像素不一致：滤波' .. mode .. ' 偏移' .. bias)
        -- 相等时身体先入层，应仍显示；仅身体比遮罩浅时打洞，原值零不参与打洞。
        for y = 0, 255 do
            for x = 0, 255 do
                local value = string.unpack('I4', actual, y * pitch + x * 4 + 1)
                local want = bias < 0 and (x > 0 or y > 0) and 0 or 0xFFFFFFFF
                check(value == want, '端到端深度比较错误：' .. y .. ',' .. x .. ' 偏移' .. bias)
            end
        end
    end
    mask:CacheClear()
    check(compose(mask, 0) == compose(reference, 0), '清缓存后灰16原图未保留')
end
target:Free()

local tiny = '\0\0\0\0\1\0\1\0\255\255'
local image = png(tiny, 2, 2)
local valid = spr(image)
local geometry, info = depth(spr(image, {frames = {{0, 0, 0, 1, 1, -2, 3}, {0, 1, 1, 1, 1, 4, -5}}}))
check(geometry and info.frame == 2 and info.width == 1 and info.height == 1, '多帧裁剪信息错误')
local second = geometry:GetFrameInfo(1)
check(second.x == 4 and second.y == -5 and second.width == 1 and second.height == 1, '第二帧锚点错误')
local sf = assert(geometry:GetFrame(1))
local rendered = pixels(sf)
check((string.unpack('I4', rendered) >> 24) == 255, '子矩形采样未命中非零灰度')
sf:Free()
local f64 = {}
for i = 1, 64 do f64[i] = {0, 0, 0, 2, 2, 0, 0} end
local many, many_info = depth(spr(image, {frames = f64}))
check(many and many_info.total == 64 and many:GetFrameInfo(63).width == 2, '64帧边界错误')
f64[65] = f64[1]
local bad = {
    {'空字节', ''}, {'坏签名', 'BAD!'}, {'截断', valid:sub(1, -2)},
    {'外层长度', replace(valid, 9, pack('<I4', 1))}, {'版本', replace(valid, 5, pack('<I4', 2))},
    {'内部签名', replace(valid, 17, 'XX')}, {'方向数', spr(image, {groups = 2})},
    {'多图', spr(image, {images = 2})}, {'超帧', spr(image, {frames = f64})},
    {'SPR类型', spr(image, {typ = 0})}, {'图像索引', spr(image, {image = 1})},
    {'负坐标', spr(image, {x = -1})}, {'帧矩形越界', spr(image, {w = 3})},
    {'PNG签名', spr(replace(image, 1, 'BAD!'))}, {'PNG CRC', spr(replace(image, 30, '\0\0\0\0'))},
    {'非16位', spr(png(tiny, 2, 2, {depth = 8}))}, {'非灰度', spr(png(tiny, 2, 2, {color = 2}))},
    {'交错', spr(png(tiny, 2, 2, {interlace = 1}))}, {'零宽', spr(png(tiny, 0, 2))},
    {'宽越界', spr(png(tiny, 16385, 2))}, {'高越界', spr(png(tiny, 2, 4097))},
    {'像素量越界', spr(png(tiny, 16384, 4096))}, {'滤波错误', spr(png('\5' .. tiny:sub(2), 2, 2))},
    {'解压不足', spr(png(tiny:sub(1, -2), 2, 2))}, {'解压过量', spr(png(tiny .. '\0', 2, 2))},
    {'压缩放大', spr(png(string.rep('\0', 65536), 2, 2))},
    {'压缩损坏', spr(png(tiny, 2, 2, {compressed = 'not zlib'}))},
    {'压缩流截断', spr(png(tiny, 2, 2, {compressed = zlib.compress(tiny):sub(1, -2)}))},
    {'压缩流尾垃圾', spr(png(tiny, 2, 2, {compressed = zlib.compress(tiny) .. 'junk'}))},
    {'额外PNG数据', spr(image .. 'junk')}, {'缺IEND', spr(image:sub(1, -13))},
    {'未知块', spr(png(tiny, 2, 2, {before = chunk('PLTE', 'abc')}))},
    {'分离IDAT', spr(png(tiny, 2, 2, {after = chunk('gAMA', pack('>I4', 100000)) .. chunk('IDAT', 'x')}))},
    {'额外IDAT尾部', spr(png(tiny, 2, 2, {after = chunk('IDAT', 'x')}))},
    {'错误压缩算法', spr(png(tiny, 2, 2, {compression = 1}))},
    {'输入过长', string.rep('\0', 8 * 1024 * 1024 + 1)},
}
for _, case in ipairs(bad) do
    local ok, result, why = pcall(depth, case[2])
    check(ok and result == nil and type(why) == 'string', case[1] .. '未按nil/error拒绝')
    check(depth(valid) ~= nil, case[1] .. '失败污染后续加载')
end
for _, value in ipairs({false, true, 123, {}}) do
    local ok, result, why = pcall(depth, value)
    check(ok and result == nil and type(why) == 'string', '参数类型未拒绝')
end
local nothing, why = depth(nil)
check(nothing == nil and type(why) == 'string', 'nil参数未拒绝')
collectgarbage('collect')
print('PASS 灰16原生入口：65536值×5滤波×3深度边界、像素等价、多帧几何、坏输入恢复；断言 ' .. assertions)
