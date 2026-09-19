-- 由同目录 Python 夹具驱动：真实下载器/HTTP，必须使用本轮 CI ggelua.dll。
local engine, base, closed_port = assert(arg[1]), assert(arg[2]), assert(arg[3])
local gge = assert(package.loadlib(engine .. '/ggelua.dll', 'luaopen_ggelua'))()
local download = assert(package.loadlib(engine .. '/ggelua.dll', 'luaopen_ghv_download'))()
local function done(handle)
    assert(type(handle.GetDiagnostics) == 'function', 'CI产物缺少GetDiagnostics，禁止用旧DLL冒充通过')
    local deadline = os.time() + 20
    while true do
        local _, _, status = handle:GetState()
        if status == 100 or status < 0 then return status, handle:GetDiagnostics() end
        assert(os.time() <= deadline, '真实下载未在上限内结算')
        gge.delay(10)
    end
end
local function raw(path, expected)
    local status, diag = done(download(base .. path, nil, nil, 2))
    assert(status == expected and diag.available and diag.complete and diag.schema == 1)
    assert(diag.attempt_total == 1 and #diag.attempts == 1)
    local a = diag.attempts[1]
    assert(a.url == base .. path and a.status == status and a.elapsed_ms >= 0)
    assert(a.ret == 0 and a.http_status == (expected == 100 and 200 or -expected))
    if expected < 0 then
        assert(diag.first_failure.status == expected and diag.last_failure.status == expected)
        assert(a.stage == 'http' and a.error ~= '')
    else assert(diag.first_failure == nil and a.bytes == 3 and a.stage == 'ready') end
end
raw('/ok', 100)
raw('/502', -502)
raw('/404', -404)
local status, diag = done(download('http://127.0.0.1:' .. closed_port .. '/closed', nil, nil, 1))
assert(status < 0 and diag.attempts[1].ret ~= 0 and diag.attempts[1].http_status == nil)
assert(diag.attempts[1].stage == 'transport', '连接失败不能伪造HttpResponse默认200')
status, diag = done(download(base .. '/slow', nil, nil, 1))
assert(status < 0 and diag.attempts[1].ret ~= 0 and diag.attempts[1].http_status == nil)

for _, allow in ipairs({false, true}) do
    status, diag = done(download.CDN({resource_type = 'spr', policy = 'part', pid = 5, eid = 41403,
        dir = 7, act = 'ride_stand', ver = '', jsoncmd = '', fallback_act = 'stand',
        allow_direct = allow, disable_render = true, mirror_only = true}, nil, 2))
    assert(status == -404 and diag.complete and not diag.truncated)
    local fallback_direct = false
    for _, a in ipairs(diag.attempts) do
        assert(a.pid == 5 and a.eid == 41403 and a.dir == 7 and a.ver == '' and a.jsoncmd == '')
        assert(a.ret == 0 and a.http_status == 404 and a.url:find(base, 1, true) == 1)
        if a.kind == 'fallback_direct' then fallback_direct = true end
        if not allow then assert(not a.url:find('/avtres_full_dir/', 1, true), '禁止direct仍生成回退direct') end
    end
    assert(fallback_direct == allow, 'fallback_direct没有遵守allow_direct')
    assert(diag.first_failure.url == diag.attempts[1].url)
    assert(diag.last_failure.url == diag.attempts[#diag.attempts].url)
end
print('真实HTTP原生诊断：200/502/404/连接失败/超时/候选身份/direct门通过')
