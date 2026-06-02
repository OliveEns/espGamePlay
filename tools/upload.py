#!/usr/bin/env python3
"""
upload.py - 游戏文件上传工具
支持串口 (serial) 和 TCP (tcp) 两种方式，协议与 download_manager 匹配。
用法：
  python upload.py serial <COM端口> <游戏文件.game>
  python upload.py tcp <IP地址> <游戏文件.game>
示例：
  python upload.py serial COM3 test.game
  python upload.py tcp 192.168.4.1 test.game
"""

import sys
import struct
import time
import argparse
import os
import zlib
import socket

# ---------- 协议常量 ----------
FRAME_HEADER = b'\x55\xAA'
CMD_HANDSHAKE = 0x01
CMD_UPLOAD_START = 0x02
CMD_UPLOAD_DATA = 0x03
CMD_UPLOAD_END = 0x04
CMD_UPLOAD_CANCEL = 0x05
CMD_HANDSHAKE_RESP = 0x81
CMD_UPLOAD_ACK = 0x82
CMD_UPLOAD_PROGRESS = 0x83

STATUS_OK = 0x00
STATUS_FILE_OPEN_ERR = 0x01
STATUS_WRITE_ERR = 0x02
STATUS_CRC_ERR = 0x03
STATUS_BUSY = 0x04

CHUNK_SIZE = 1024


# ---------- CRC16/CCITT ----------
def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
    return crc & 0xFFFF


# ---------- 发送帧 ----------
def send_frame(stream, cmd: int, data: bytes):
    frame = FRAME_HEADER + struct.pack('>B', cmd) + struct.pack('>H', len(data)) + data
    crc = crc16_ccitt(frame[2:])
    frame += struct.pack('>H', crc)
    if hasattr(stream, 'write'):
        stream.write(frame)
    else:
        stream.send(frame)


# ---------- 接收并解析响应帧 ----------
def recv_response(stream, timeout=2.0):
    start_time = time.time()
    buf = b''
    is_socket = not hasattr(stream, 'read')

    while time.time() - start_time < timeout:
        try:
            if is_socket:
                stream.settimeout(0.1)
                try:
                    b = stream.recv(1)
                    if not b:
                        time.sleep(0.01)
                        continue
                except socket.timeout:
                    continue
            else:
                b = stream.read(1)
                if not b:
                    time.sleep(0.01)
                    continue
        except Exception as e:
            print(f"接收错误: {e}")
            return None, None, None

        buf += b
        if len(buf) >= 7:
            pos = buf.find(FRAME_HEADER)
            if pos == -1:
                buf = buf[-6:] if len(buf) > 6 else b''
                continue
            if pos > 0:
                buf = buf[pos:]

            if len(buf) < 7:
                continue

            cmd = buf[2]
            data_len = (buf[3] << 8) | buf[4]
            total_len = 7 + data_len
            if len(buf) < total_len:
                continue

            frame_data = buf[5:5+data_len]
            recv_crc = (buf[5+data_len] << 8) | buf[6+data_len]
            calc_crc = crc16_ccitt(buf[2:5+data_len])
            if calc_crc != recv_crc:
                print("CRC校验错误，丢弃帧")
                buf = buf[1:]
                continue

            # 有效帧
            if cmd == CMD_UPLOAD_ACK:
                if len(frame_data) >= 1:
                    status = frame_data[0]
                    return cmd, frame_data, status
                else:
                    return cmd, frame_data, None
            elif cmd == CMD_UPLOAD_PROGRESS:
                if len(frame_data) >= 4:
                    progress = struct.unpack('>I', frame_data[:4])[0]
                    return cmd, frame_data, progress
                else:
                    return cmd, frame_data, None
            else:
                return cmd, frame_data, None

    print("等待响应超时")
    return None, None, None


# ---------- 串口上传 ----------
def upload_serial(port: str, filepath: str, baudrate: int = 115200):
    try:
        import serial
    except ImportError:
        print("错误：缺少 pyserial 库，请执行 pip install pyserial")
        sys.exit(1)

    with open(filepath, 'rb') as f:
        file_data = f.read()
    base_name = os.path.basename(filepath).replace('.game', '')
    total_size = len(file_data)

    ser = serial.Serial(port, baudrate, timeout=2)
    print(f"打开串口 {port} 成功")

    start_data = base_name.encode() + b'\x00' + struct.pack('>II', total_size, 0)
    send_frame(ser, CMD_UPLOAD_START, start_data)
    print("已发送 START 命令，等待 ACK...")
    cmd, data, status = recv_response(ser)
    if cmd != CMD_UPLOAD_ACK or status != STATUS_OK:
        print(f"错误：设备拒绝上传，状态码: {status if status is not None else '无响应'}")
        ser.close()
        return False

    print("设备已接受上传，开始发送数据...")
    offset = 0
    while offset < total_size:
        chunk = file_data[offset:offset + CHUNK_SIZE]
        block = struct.pack('>I', offset) + chunk
        send_frame(ser, CMD_UPLOAD_DATA, block)
        offset += len(chunk)
        percent = offset * 100 // total_size
        print(f"\r进度: {percent}% ({offset}/{total_size})", end='', flush=True)
        time.sleep(0.01)

    print("\n所有数据已发送，等待最终校验...")
    crc = zlib.crc32(file_data) & 0xFFFFFFFF
    send_frame(ser, CMD_UPLOAD_END, struct.pack('>I', crc))

    # 循环接收直到 ACK
    final_status = None
    start_time = time.time()
    while time.time() - start_time < 5.0:
        cmd, _, val = recv_response(ser, timeout=1.0)
        if cmd is None:
            continue
        if cmd == CMD_UPLOAD_ACK:
            final_status = val
            break
        elif cmd == CMD_UPLOAD_PROGRESS:
            print(f"\r设备端进度: {val}/{total_size} bytes", end='', flush=True)
        else:
            print(f"\r收到其他命令: 0x{cmd:02X}", end='', flush=True)

    ser.close()
    if final_status == STATUS_OK:
        print("\n上传成功！设备已保存游戏文件。")
        return True
    else:
        print(f"\n上传失败，设备返回状态码: {final_status if final_status is not None else '无响应'}")
        return False


# ---------- TCP 上传 ----------
def upload_tcp(ip: str, filepath: str, port: int = 8888):
    with open(filepath, 'rb') as f:
        file_data = f.read()
    base_name = os.path.basename(filepath).replace('.game', '')
    total_size = len(file_data)

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((ip, port))
    print(f"连接到 {ip}:{port} 成功")

    start_data = base_name.encode() + b'\x00' + struct.pack('>II', total_size, 0)
    send_frame(sock, CMD_UPLOAD_START, start_data)
    print("已发送 START 命令，等待 ACK...")
    cmd, data, status = recv_response(sock)
    if cmd != CMD_UPLOAD_ACK or status != STATUS_OK:
        print(f"错误：设备拒绝上传，状态码: {status if status is not None else '无响应'}")
        sock.close()
        return False

    print("设备已接受上传，开始发送数据...")
    offset = 0
    while offset < total_size:
        chunk = file_data[offset:offset + CHUNK_SIZE]
        block = struct.pack('>I', offset) + chunk
        send_frame(sock, CMD_UPLOAD_DATA, block)
        offset += len(chunk)
        percent = offset * 100 // total_size
        print(f"\r进度: {percent}% ({offset}/{total_size})", end='', flush=True)
        time.sleep(0.01)

    print("\n所有数据已发送，等待最终校验...")
    crc = zlib.crc32(file_data) & 0xFFFFFFFF
    send_frame(sock, CMD_UPLOAD_END, struct.pack('>I', crc))

    final_status = None
    start_time = time.time()
    while time.time() - start_time < 5.0:
        cmd, _, val = recv_response(sock, timeout=1.0)
        if cmd is None:
            continue
        if cmd == CMD_UPLOAD_ACK:
            final_status = val
            break
        elif cmd == CMD_UPLOAD_PROGRESS:
            print(f"\r设备端进度: {val}/{total_size} bytes", end='', flush=True)
        else:
            print(f"\r收到其他命令: 0x{cmd:02X}", end='', flush=True)

    sock.close()
    if final_status == STATUS_OK:
        print("\n上传成功！设备已保存游戏文件。")
        return True
    else:
        print(f"\n上传失败，设备返回状态码: {final_status if final_status is not None else '无响应'}")
        return False


# ---------- 主函数 ----------
def main():
    parser = argparse.ArgumentParser(description="上传 .game 文件到 ESP32-C3 游戏机")
    subparsers = parser.add_subparsers(dest='mode', help='传输模式')

    parser_serial = subparsers.add_parser('serial', help='通过串口上传')
    parser_serial.add_argument('port', help='串口名称, 如 COM3 或 /dev/ttyUSB0')
    parser_serial.add_argument('file', help='要上传的 .game 文件路径')
    parser_serial.add_argument('-b', '--baudrate', type=int, default=115200, help='波特率，默认115200')

    parser_tcp = subparsers.add_parser('tcp', help='通过 TCP 上传')
    parser_tcp.add_argument('ip', help='ESP32 的 IP 地址，如 192.168.4.1')
    parser_tcp.add_argument('file', help='要上传的 .game 文件路径')
    parser_tcp.add_argument('-p', '--port', type=int, default=8888, help='端口号，默认8888')

    args = parser.parse_args()

    if args.mode == 'serial':
        if not os.path.isfile(args.file):
            print(f"错误：文件 {args.file} 不存在")
            sys.exit(1)
        success = upload_serial(args.port, args.file, args.baudrate)
    elif args.mode == 'tcp':
        if not os.path.isfile(args.file):
            print(f"错误：文件 {args.file} 不存在")
            sys.exit(1)
        success = upload_tcp(args.ip, args.file, args.port)
    else:
        parser.print_help()
        sys.exit(1)

    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()