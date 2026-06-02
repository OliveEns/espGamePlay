#!/usr/bin/env python3
# pack_game.py - 将 Lua 脚本打包成 .game 文件
import struct
import sys
import os
import zlib

def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF

def pack_lua_to_game(lua_file_path, game_name, author, output_path=None):
    # 读取 Lua 脚本
    with open(lua_file_path, 'rb') as f:
        script_data = f.read()
    
    script_size = len(script_data)
    script_crc = crc32(script_data)
    
    # 固定文件头 (64字节)
    magic = b'GM01'
    version = 0x01
    reserved = 0
    game_name_len = len(game_name)
    # game_name 固定32字节，不足补0
    game_name_bytes = game_name.encode('utf-8')[:32].ljust(32, b'\x00')
    # author 固定16字节
    author_bytes = author.encode('utf-8')[:16].ljust(16, b'\x00')
    
    header = struct.pack('<4sBBH32s16sII',
        magic,           # 4 bytes
        version,         # 1 byte
        reserved,        # 1 byte
        game_name_len,   # 2 bytes
        game_name_bytes, # 32 bytes
        author_bytes,    # 16 bytes
        script_crc,      # 4 bytes
        script_size      # 4 bytes
    )
    # 总长度应为 4+1+1+2+32+16+4+4 = 64 bytes
    assert len(header) == 64
    
    # 构建最终文件
    game_data = header + script_data
    
    if output_path is None:
        output_path = os.path.splitext(lua_file_path)[0] + '.game'
    with open(output_path, 'wb') as f:
        f.write(game_data)
    print(f"Packed to {output_path} (size={len(game_data)} bytes)")

if __name__ == '__main__':
    if len(sys.argv) < 4:
        print("Usage: pack_game.py <input.lua> <game_name> <author> [output.game]")
        sys.exit(1)
    lua_file = sys.argv[1]
    game_name = sys.argv[2]
    author = sys.argv[3]
    out_file = sys.argv[4] if len(sys.argv) > 4 else None
    pack_lua_to_game(lua_file, game_name, author, out_file)