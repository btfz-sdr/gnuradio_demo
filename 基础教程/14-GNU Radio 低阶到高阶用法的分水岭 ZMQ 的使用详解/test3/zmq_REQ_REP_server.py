#!/usr/bin/python3
# -*- coding: utf-8 -*-

import pmt
import zmq

context = zmq.Context()

# 1. REP 套接字：监听来自 zmq_req.py (REQ 客户端) 的请求
rep_sock = context.socket(zmq.REP)
rep_sock.bind("tcp://127.0.0.1:50247")
print(f"REP listening on tcp://127.0.0.1:50247 (for zmq_req.py)")

# 2. REQ 套接字：连接到 zmq_rep.py (REP 服务器)
req_sock = context.socket(zmq.REQ)
req_sock.connect("tcp://127.0.0.1:50246")
print(f"REQ connected to tcp://127.0.0.1:50246 (zmq_rep.py)")
print("=" * 60)

# 注册 req_sock 以等待 zmq_rep.py 的回复
poller = zmq.Poller()
poller.register(req_sock, zmq.POLLIN)

while True:
    try:
        # 1. 阻塞等待 zmq_req.py 发送请求
        request_data = rep_sock.recv()
        
        # 2. 将请求透传发送给 zmq_rep.py
        req_sock.send(request_data)
        
        # 3. 等待 zmq_rep.py 的响应（带 5 秒超时）
        socks = dict(poller.poll(5000))
        
        if req_sock in socks and socks[req_sock] == zmq.POLLIN:
            resp_bytes = req_sock.recv()
            
            # 解析 zmq_rep.py 返回的数据 (PMT 格式)
            try:
                p_val = pmt.deserialize_str(resp_bytes)
                if pmt.is_symbol(p_val):
                    val_str = pmt.symbol_to_string(p_val)
                elif pmt.is_string(p_val):
                    val_str = pmt.string_to_python_string(p_val)
                else:
                    val_str = str(pmt.to_python(p_val))
                
                # 将 "TEST" 转换为小写 "test"
                converted_str = val_str.lower()
                print(f"[Server] Converted: {val_str} -> {converted_str}")
                
                # 重新打包为 PMT Symbol 并序列化
                reply_bytes = pmt.serialize_str(pmt.intern(converted_str))
            except Exception as e:
                print(f"[Error] Failed to parse/convert PMT: {e}")
                reply_bytes = resp_bytes
            
            # 4. 回复给 zmq_req.py
            rep_sock.send(reply_bytes)
        else:
            print("[WARNING] Timeout waiting for zmq_rep.py")
            # 超时重置 REQ socket 以恢复状态
            req_sock.close()
            req_sock = context.socket(zmq.REQ)
            req_sock.connect("tcp://127.0.0.1:50246")
            poller.register(req_sock, zmq.POLLIN)
            
            err_pmt = pmt.serialize_str(pmt.intern("error"))
            rep_sock.send(err_pmt)

    except KeyboardInterrupt:
        break
    except Exception as e:
        print(f"[Error] {e}")
        import traceback
        traceback.print_exc()

print("Shutting down...")
