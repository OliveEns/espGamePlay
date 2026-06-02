-- test.lua
-- 游戏机功能测试脚本
-- 实现：移动方块（方向键控制），显示当前按键、帧率、堆栈信息

-- 游戏状态
local player = {
    x = 120,
    y = 120,
    size = 20,
    color = Game.rgb(255, 255, 0)   -- 黄色
}

-- 帧率统计
local frame_count = 0
local fps = 0
local last_fps_time = 0

-- 辅助函数：显示调试信息
function draw_debug_info()
    local y = 0
    -- 显示按键
    local key = Game.get_key()
    Game.draw_text(0, y, "Key: " .. key, Game.rgb(255, 255, 255))
    y = y + 12
    
    -- 显示坐标
    local pos_str = string.format("Pos: %d,%d", player.x, player.y)
    Game.draw_text(0, y, pos_str, Game.rgb(255, 255, 255))
    y = y + 12
    
    -- 显示帧率
    Game.draw_text(0, y, "FPS: " .. fps, Game.rgb(0, 255, 0))
    y = y + 12
    
    -- 显示可用堆内存（通过系统 API，需另外绑定，这里仅作示例）
    -- Game.draw_text(0, y, "Heap: " .. Game.get_free_heap() .. " bytes", Game.rgb(0, 255, 255))
end

-- 游戏初始化
function on_init()
    Game.clear_screen(Game.rgb(0, 0, 0))
    print("Test game initialized")
    last_fps_time = Game.get_tick()
end

-- 每帧更新逻辑
function on_update(dt)
    -- 获取按键
    local key = Game.get_key()
    
    -- 移动速度（像素/秒）
    local speed = 150 * dt
    
    if key == "UP" then
        player.y = player.y - speed
    elseif key == "DOWN" then
        player.y = player.y + speed
    elseif key == "LEFT" then
        player.x = player.x - speed
    elseif key == "RIGHT" then
        player.x = player.x + speed
    end
    
    -- 边界限制
    if player.x < 0 then player.x = 0 end
    if player.y < 0 then player.y = 0 end
    if player.x + player.size > 240 then player.x = 240 - player.size end
    if player.y + player.size > 240 then player.y = 240 - player.size end
    
    -- 帧率统计
    frame_count = frame_count + 1
    local now = Game.get_tick()
    if now - last_fps_time >= 1000 then
        fps = frame_count
        frame_count = 0
        last_fps_time = now
    end
end

-- 渲染
function on_render()
    -- 清屏（黑色背景）
    Game.clear_screen(Game.rgb(0, 0, 0))
    
    -- 绘制玩家方块
    Game.draw_rect(player.x, player.y, player.size, player.size, player.color)
    
    -- 绘制中心十字辅助线
    local center_x = 120
    local center_y = 120
    Game.draw_rect(center_x - 1, center_y - 10, 2, 20, Game.rgb(128, 128, 128))
    Game.draw_rect(center_x - 10, center_y - 1, 20, 2, Game.rgb(128, 128, 128))
    
    -- 显示调试信息
    draw_debug_info()
    
    -- 提示按返回键退出
    Game.draw_text(0, 230, "PRESS BACK TO EXIT", Game.rgb(255, 0, 0))
    
    -- 更新显示（函数内已清屏，实际需调用 update_display，但驱动直接写入，留空即可）
    Game.update_display()
end