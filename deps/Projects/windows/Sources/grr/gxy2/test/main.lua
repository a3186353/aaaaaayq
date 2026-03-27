-- @Author              : GGELUA
-- @Last Modified by    : Please set LastEditors
-- @Date                : 2022-03-23 10:09:27
-- @Last Modified time  : 2022-05-08 08:15:23

if gge.isdebug and os.getenv('LOCAL_LUA_DEBUGGER_VSCODE') == '1' then
    package.loaded['lldebugger'] = assert(loadfile(os.getenv('LOCAL_LUA_DEBUGGER_FILEPATH')))()
    require('lldebugger').start()
end
local SDL = require('SDL')
local GGF = require('GGE.函数')
引擎 =
    require 'SDL.窗口' {
    标题 = 'GGELUA_窗口',
    宽度 = 800,
    高度 = 600,
    帧率 = 60
}
task={}
local ue = --userevent
    SDL.RegisterUserEvent(
    function(code, data)
        code = code & 0xFFFFFFFF --转无符号
        local r = {map:GetResult(data)}
        if type(r[1]) == 'number' then
            for i, v in ipairs(r[3]) do
            task[v]=true
                map:GetMask(v,true)
            end
        else
            local t=r[1]
            print(task[t],t.offset,r[2])
        end
    end
)

function 导出地图(file)
    local map, info = require('gxy2.map')(file, ue, 123)
    if map then
        print(file, info.colnum, info.rownum)
        sf = require('SDL.图像')(info.width, info.height)
        sf:渲染开始()
        local n = 0
        local x, y = 0, 0
        for h = 1, info.rownum do
            for l = 1, info.colnum do
                require('SDL.图像')(map:GetMap(n)):显示(x, y)
                n = n + 1
                x = x + 320
            end
            y = y + 240
            x = 0
        end

        sf:渲染结束()
        sf:保存文件('newscene/' .. GGF.取文件名(file, true) .. '.jpg', 'JPG')
    end
end

function 导出遮罩(file)
    local map, info = require('gxy2.map')(file, ue, 123)

    if map then
        sf = require('SDL.图像')(info.width, info.height)
        sf:渲染开始()
        local t = {}
        for i = 0, info.mapnum - 1 do
            for _, v in ipairs(map:GetMaskInfo(i)) do
                if not t[v.x .. '|' .. v.y] then
                    require('SDL.图像')(map:GetMask(v)):显示(v.x, v.y)
                    t[v.x .. '|' .. v.y] = true
                else
                    --print(v.x, v.y, v.offset)
                end
            end
        end
        sf:渲染结束()
        sf:保存文件('newscene/' .. GGF.取文件名(file, true) .. '.jpg', 'JPG')
    end
end

function 引擎:初始化()
    --导出遮罩([[D:\大话西游2\newscene\1173.map]])
    -- for file in GGF.遍历文件([[D:\大话西游2\newscene/]]) do
    --     if not GGF.判断文件('newscene/' .. GGF.取文件名(file, true) .. '.jpg') then
    --         print(file)
    --         导出遮罩(file)
    --     end
    -- end
    -- for file in GGF.遍历文件([[D:\大话西游2\newscene]]) do
    --     if file:sub(-3) == 'map' then
    --         local map, info = require('gxy2.map')(file, ue, 123)
    --         local bt = map:GetBlock(0)
    --         if not bt['GEPJ'] and not bt['GNP\0'] then
    --             print(file)
    --             for k, v in pairs(bt) do
    --                 print(k, #v)
    --             end
    --         end
    --     end
    -- end
    map, info = require('gxy2.map')([[D:\大话西游2\newscene\1478.map]], ue, 123)

    map:SetMode(0x9527)
    for i = 0, info.mapnum - 1 do
        map:GetMap(i, true)
    end

    spr = require('SDL.精灵')(sf)
    spr1 = require('SDL.精灵')(sf1)
end

function 引擎:更新事件(dt, x, y)
end

function 引擎:渲染事件(dt, x, y)
    if self:渲染开始(0x70, 0x70, 0x70) then
        --spr1:显示(0, 0)
        --spr:显示(x, y)
        self:渲染结束()
    end
end

function 引擎:窗口事件(ev)
    if ev == SDL.WINDOWEVENT_CLOSE then
        引擎:关闭()
    end
end

function 引擎:键盘事件(KEY, KMOD, 状态, 按住)
    if not 状态 then --弹起
        if KEY == SDL.KEY_F1 then
            map:__gc()
            collectgarbage()
            print('F1')
        elseif KEY == SDL.KEY_F2 then
            map:Clear()
            collectgarbage()
            print('F2')
        end
    end
    if KMOD & SDL.KMOD_LCTRL ~= 0 then
        print('左CTRL', 按住)
    end
    if KMOD & SDL.KMOD_ALT ~= 0 then
        print('左右ALT', 按住)
    end
end

function 引擎:鼠标事件()
end

function 引擎:输入事件()
end

function 引擎:销毁事件()
end
