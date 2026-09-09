#!/usr/bin/python3
# -*- coding: utf-8 -*-

# zmq_PUSH_PULL_server.py

import sys
import pmt
import zmq

_debug = 0          # set to zero to turn off diagnostics

# create a PUSH socket
_PROTOCOL = "tcp://"
_SERVER = "127.0.0.1"          # localhost
_PUSH_PORT = ":50252"
_PUSH_ADDR = _PROTOCOL + _SERVER + _PUSH_PORT
if (_debug):
    print ("'zmq_PUSH_PULL_server' version 20068.1 binding to:", _PUSH_ADDR)
push_context = zmq.Context()
if (_debug):
    assert (push_context)
push_sock = push_context.socket (zmq.PUSH)
if (_debug):
    assert (push_sock)
rc = push_sock.bind (_PUSH_ADDR)
if (_debug):
    assert (rc == None)

# create a PULL socket
_PROTOCOL = "tcp://"
_SERVER = "127.0.0.1"          # localhost
_PULL_PORT = ":50251"
_PULL_ADDR = _PROTOCOL + _SERVER + _PULL_PORT
if (_debug):
    print ("'zmq_PUSH_PULL_server' connecting to:", _PULL_ADDR)
pull_context = zmq.Context()
if (_debug):
    assert (pull_context)
pull_sock = pull_context.socket (zmq.PULL)
if (_debug):
    assert (pull_sock)
rc = pull_sock.connect (_PULL_ADDR)
if (_debug):
    assert (rc == None)

while True:
    #  Wait for next request from client
    data = pull_sock.recv()
    message = pmt.to_python(pmt.deserialize_str(data))
    # print("Received request: %s" % message)

    output = message.upper()    # capitalize message

    #  Send reply back to client
    push_sock.send (pmt.serialize_str(pmt.to_pmt(output)))

