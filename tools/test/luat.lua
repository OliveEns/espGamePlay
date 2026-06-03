-- test_game.lua
-- 测试 ST7789 + Lua 绑定层的完整功能
-- 显示动态图形、按键检测、帧计时等

-- 颜色常量
local COLOR_RED     = Game.rgb(255, 0, 0)
local COLOR_GREEN   = Game.rgb(0, 255, 0)
local COLOR_BLUE    = Game.rgb(0, 0, 255)
local COLOR_YELLOW  = Game.rgb(255, 255, 0)
local COLOR_CYAN    = Game.rgb(0, 255, 255)
local COLOR_MAGENTA = Game.rgb(255, 0, 255)
local COLOR_WHITE   = Game.rgb(255, 255, 255)
local COLOR_BLACK   = Game.rgb(0, 0, 0)
local COLOR_GRAY    = Game.rgb(128, 128, 128)
local COLOR_ORANGE  = Game.rgb(255, 165, 0)

-- 全局状态
local ball_x = 120
local ball_y = 120
local ball_vx = 50      -- 像素/秒
local ball_vy = 30
local ball_color = COLOR_RED   -- 初始颜色
local last_key = "NONE"
local frame_count = 0
local fps_display = 0
local elapsed_time = 0

function on_init()
    Game.clear_screen(COLOR_BLACK)
    print("测试脚本启动，按方向键/OK 测试，按 BACK 键退出")
end

function on_update(dt)
    -- 更新位置
    ball_x = ball_x + ball_vx * dt
    ball_y = ball_y + ball_vy * dt

    local radius = 10
    if ball_x - radius < 0 then
        ball_x = radius
        ball_vx = -ball_vx
    elseif ball_x + radius > 239 then
        ball_x = 239 - radius
        ball_vx = -ball_vx
    end

    if ball_y - radius < 0 then
        ball_y = radius
        ball_vy = -ball_vy
    elseif ball_y + radius > 239 then
        ball_y = 239 - radius
        ball_vy = -ball_vy
    end

    local key = Game.get_key()
    if key ~= "NONE" then
        last_key = key
    end

    -- 根据按键改变小球颜色
    if key == "UP" then
        ball_color = COLOR_CYAN
    elseif key == "DOWN" then
        ball_color = COLOR_MAGENTA
    elseif key == "LEFT" then
        ball_color = COLOR_YELLOW
    elseif key == "RIGHT" then
        ball_color = COLOR_ORANGE
    elseif key == "OK" then
        ball_color = COLOR_WHITE
    else
        ball_color = COLOR_RED
    end

    -- 帧率统计
    frame_count = frame_count + 1
    elapsed_time = elapsed_time + dt
    if elapsed_time >= 0.5 then
        fps_display = frame_count / elapsed_time
        frame_count = 0
        elapsed_time = 0
    end
end

function on_render()
    Game.clear_screen(COLOR_BLACK)

    -- 静态图形
    Game.draw_rect_border(10, 10, 60, 40, COLOR_GREEN)
    Game.draw_rect(170, 190, 60, 40, COLOR_BLUE)
    Game.draw_circle(60, 200, 20, COLOR_YELLOW)
    Game.fill_circle(200, 50, 15, COLOR_MAGENTA)
    Game.draw_line(10, 10, 229, 229, COLOR_GRAY)
    Game.draw_triangle(180, 80, 210, 120, 150, 120, COLOR_CYAN)

    -- 移动小球（坐标取整，避免类型错误）
    local ix = math.floor(ball_x)
    local iy = math.floor(ball_y)
    Game.fill_circle(ix, iy, 10, ball_color)

    -- 文字信息
    Game.draw_text(10, 55, "Lua Game Test", COLOR_WHITE)
    Game.draw_text(10, 75, "Last Key: " .. last_key, COLOR_GREEN)
    Game.draw_text(10, 95, string.format("FPS: %.1f", fps_display), COLOR_YELLOW)
    Game.draw_text(10, 115, "Pos: " .. ix .. "," .. iy, COLOR_CYAN)
    Game.draw_text(10, 220, "Press BACK to exit", COLOR_GRAY)
end