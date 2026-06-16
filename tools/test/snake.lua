-- snake.lua
-- 贪吃蛇游戏 for ESP32-C3 Game Console
-- Author: Oliver

-- 常量定义
local SCREEN_W = 240
local SCREEN_H = 240
local CELL_SIZE = 8
-- 顶部留出2格给信息栏 + 边框
local GRID_OFFSET_X = 1
local GRID_OFFSET_Y = 16
local GRID_W = 28
local GRID_H = 28
local MOVE_INTERVAL = 0.15     -- 蛇移动间隔（秒）

-- 方向
local DIR_UP    = {x =  0, y = -1}
local DIR_DOWN  = {x =  0, y =  1}
local DIR_LEFT  = {x = -1, y =  0}
local DIR_RIGHT = {x =  1, y =  0}

-- 游戏状态
local STATE_PLAYING = 1
local STATE_GAMEOVER = 2

-- 颜色
local C_BG      = Game.rgb(10, 10, 25)
local C_HEAD    = Game.rgb(0, 255, 80)
local C_BODY    = Game.rgb(0, 200, 50)
local C_FOOD    = Game.rgb(255, 50, 50)
local C_GRID    = Game.rgb(30, 30, 50)
local C_BORDER  = Game.rgb(80, 80, 100)
local C_WHITE   = Game.rgb(255, 255, 255)
local C_YELLOW  = Game.rgb(255, 255, 0)
local C_INFO_BG = Game.rgb(0, 0, 0)

-- 游戏变量
local snake
local direction
local next_direction
local food_x, food_y
local score
local state
local move_timer
local last_key
local prev_tail       -- 上一帧的尾巴坐标（用于擦除）

-- 网格坐标转屏幕像素坐标（考虑偏移）
local function cell_to_px(cx, cy, margin)
    margin = margin or 1
    local px = GRID_OFFSET_X + (cx * CELL_SIZE) + margin
    local py = GRID_OFFSET_Y + (cy * CELL_SIZE) + margin
    return px, py, CELL_SIZE - margin * 2
end

-- 随机数种子
local function seed_random()
    math.randomseed(Game.get_tick() % 100000 + 1)
    for _ = 1, 10 do math.random() end
end

-- 生成食物
local function spawn_food()
    for _ = 1, GRID_W * GRID_H do
        local fx = math.random(0, GRID_W - 1)
        local fy = math.random(0, GRID_H - 1)
        local ok = true
        for _, s in ipairs(snake) do
            if s[1] == fx and s[2] == fy then ok = false; break end
        end
        if ok then food_x, food_y = fx, fy; return true end
    end
    return false
end

-- 绘制蛇身格子（6x6 内缩，保留网格线可见）
local function draw_cell(cx, cy, color)
    local px, py, sz = cell_to_px(cx, cy, 1)
    Game.draw_rect(px, py, sz, sz, color)
end

-- 擦除蛇尾格子（6x6 内缩 + 补网格线）
local function erase_tail_cell(cx, cy)
    draw_cell(cx, cy, C_BG)
    local px = GRID_OFFSET_X + cx * CELL_SIZE
    local py = GRID_OFFSET_Y + cy * CELL_SIZE
    Game.draw_line(px, py, px + CELL_SIZE - 1, py, C_GRID)
    Game.draw_line(px, py, px, py + CELL_SIZE - 1, C_GRID)
end

-- 擦除食物格子（8x8 全格，因为食物圆圈半径3覆盖了第7列/行）
local function erase_food_cell(cx, cy)
    local px = GRID_OFFSET_X + cx * CELL_SIZE
    local py = GRID_OFFSET_Y + cy * CELL_SIZE
    Game.draw_rect(px, py, CELL_SIZE, CELL_SIZE, C_BG)
    -- 补回被擦除的网格线
    Game.draw_line(px, py, px + CELL_SIZE - 1, py, C_GRID)
    Game.draw_line(px, py, px, py + CELL_SIZE - 1, C_GRID)
end

-- 绘制食物（仅绘制，不擦除旧食物——擦除在 move_snake 中处理）
local function draw_food_cell()
    local px, py = cell_to_px(food_x, food_y, 0)
    local cx = px + math.floor(CELL_SIZE / 2)
    local cy = py + math.floor(CELL_SIZE / 2)
    Game.fill_circle(cx, cy, math.floor(CELL_SIZE / 2) - 1, C_FOOD)
end

-- 绘制网格线（一次性绘制）
local function draw_grid_lines()
    -- 横线
    for j = 0, GRID_H do
        local y = GRID_OFFSET_Y + j * CELL_SIZE
        Game.draw_line(GRID_OFFSET_X, y, GRID_OFFSET_X + GRID_W * CELL_SIZE - 1, y, C_GRID)
    end
    -- 竖线
    for i = 0, GRID_W do
        local x = GRID_OFFSET_X + i * CELL_SIZE
        Game.draw_line(x, GRID_OFFSET_Y, x, GRID_OFFSET_Y + GRID_H * CELL_SIZE - 1, C_GRID)
    end
end

-- 初始化/重置游戏
local function init_game()
    local sx = math.floor(GRID_W / 2)
    local sy = math.floor(GRID_H / 2)
    snake = {{sx, sy}, {sx - 1, sy}, {sx - 2, sy}}
    direction = DIR_RIGHT
    next_direction = DIR_RIGHT
    score = 0
    state = STATE_PLAYING
    move_timer = 0
    last_key = "NONE"
    prev_tail = nil

    -- 清屏
    Game.clear_screen(C_BG)

    -- 绘制外边框（包围整个网格区域）
    local full_w = GRID_OFFSET_X + GRID_W * CELL_SIZE
    local full_h = GRID_OFFSET_Y + GRID_H * CELL_SIZE
    Game.draw_rect_border(0, 0, full_w, full_h, C_BORDER)

    -- 信息栏背景
    Game.draw_rect(0, 0, SCREEN_W, 14, C_INFO_BG)

    -- 一次性绘制网格线
    draw_grid_lines()

    -- 绘制初始蛇身
    for _, seg in ipairs(snake) do
        draw_cell(seg[1], seg[2], C_BODY)
    end
    -- 蛇头用亮色
    draw_cell(snake[1][1], snake[1][2], C_HEAD)

    -- 生成食物
    spawn_food()
    draw_food_cell()
end

-- 游戏结束
local function game_over()
    state = STATE_GAMEOVER
end

-- 碰撞检测
local function check_collision(hx, hy)
    if hx < 0 or hx >= GRID_W or hy < 0 or hy >= GRID_H then return true end
    for i = 1, #snake - 1 do
        if snake[i][1] == hx and snake[i][2] == hy then return true end
    end
    return false
end

-- 移动蛇（同时进行增量绘制）
local function move_snake()
    -- 防反向掉头
    if direction.x + next_direction.x ~= 0 or direction.y + next_direction.y ~= 0 then
        direction = next_direction
    end

    local head = snake[1]
    local nx, ny = head[1] + direction.x, head[2] + direction.y

    if check_collision(nx, ny) then
        game_over()
        return
    end

    -- 记录旧尾巴（稍后擦除）
    prev_tail = snake[#snake]

    -- 新头部插入
    table.insert(snake, 1, {nx, ny})

    -- 旧头部变身体色
    draw_cell(head[1], head[2], C_BODY)

    if nx == food_x and ny == food_y then
        -- 吃到食物：先全格擦除食物圆圈残留，再画蛇头，最后生成新食物
        erase_food_cell(food_x, food_y)
        draw_cell(nx, ny, C_HEAD)
        prev_tail = nil
        score = score + 10
        if not spawn_food() then game_over() end
        draw_food_cell()
    else
        -- 新头部用亮色
        draw_cell(nx, ny, C_HEAD)
        -- 擦除尾巴
        table.remove(snake)
        if prev_tail then
            erase_tail_cell(prev_tail[1], prev_tail[2])
            prev_tail = nil
        end
    end
end

-- 绘制信息栏
local function draw_info()
    Game.draw_rect(0, 0, SCREEN_W, 14, C_INFO_BG)
    Game.draw_text(2, 2, "Score: " .. score, C_YELLOW)
    Game.draw_text(120, 2, "Length: " .. #snake, C_HEAD)
end

-- 绘制结束画面
local function draw_gameover_overlay()
    local cx = math.floor(SCREEN_W / 2) - 50
    local cy = 80
    -- 遮罩
    for y = 60, 180, 3 do
        Game.draw_rect(cx - 4, y, 108, 2, Game.rgb(0, 0, 0))
    end
    Game.draw_text(cx + 2, cy - 48, "GAME OVER", Game.rgb(255, 50, 50))
    Game.draw_text(cx + 10, cy - 26, "Score: " .. score, C_WHITE)
    Game.draw_text(cx + 10, cy, "OK: Restart", C_YELLOW)
    Game.draw_text(cx + 10, cy + 16, "BACK: Menu", Game.rgb(180, 180, 180))
end

-- ================================================================
-- 生命周期
-- ================================================================

function on_init()
    seed_random()
    init_game()
end

function on_update(dt)
    local key = Game.get_key()

    if key ~= "NONE" and key ~= last_key then
        if state == STATE_PLAYING then
            if key == "UP" then next_direction = DIR_UP
            elseif key == "DOWN" then next_direction = DIR_DOWN
            elseif key == "LEFT" then next_direction = DIR_LEFT
            elseif key == "RIGHT" then next_direction = DIR_RIGHT
            end
        elseif state == STATE_GAMEOVER then
            if key == "OK" then
                init_game()
                return
            end
        end
    end
    last_key = key

    if state == STATE_PLAYING then
        move_timer = move_timer + dt
        if move_timer >= MOVE_INTERVAL then
            move_timer = move_timer - MOVE_INTERVAL
            move_snake()
        end
    end
end

function on_render()
    -- 只更新顶部信息栏文字
    draw_info()

    -- 游戏结束时显示覆盖层
    if state == STATE_GAMEOVER then
        draw_gameover_overlay()
    end
end