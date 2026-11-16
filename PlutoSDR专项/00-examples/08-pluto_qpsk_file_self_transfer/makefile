include ../toolchain/toolchain.mk

TARGET  = pluto_qpsk_file_self_transfer
PLUTO  ?= root@pluto.local

# 真机运行参数（可用命令行覆盖）
IN_FILE  ?= test.png
OUT_FILE ?= recovered.png
LO_MHZ   ?= 2500
SAMP_MHZ ?= 2.5
BW_MHZ   ?= 2.0
RX_GAIN  ?= 40
TX_ATTN  ?= 10

RUN_ARGS = -i $(IN_FILE) -o $(OUT_FILE) -l $(LO_MHZ) -s $(SAMP_MHZ) -b $(BW_MHZ) -g $(RX_GAIN) -a $(TX_ATTN)

# 3. 编译规则
build:
	$(CC) $(TARGET).c qpsk_phy.c -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -rf $(TARGET)

# 4. 上机（先编译，scp 二进制 + 测试文件到板子，再 ssh 运行）
run: clean build
	@sshpass -p '$(SSH_PASS)' scp -O $(SSH_OPTS) $(TARGET) $(PLUTO):~/
	@sshpass -p '$(SSH_PASS)' scp -O $(SSH_OPTS) doc/01-simulation/$(IN_FILE) $(PLUTO):~/$(IN_FILE)
	@echo "------------------------------------------------"
	@sshpass -p '$(SSH_PASS)' ssh $(SSH_OPTS) -t $(PLUTO) "chmod +x ~/$(TARGET); ./$(TARGET) $(RUN_ARGS)"

# 5. 宿主机仿真（用 doc/01-simulation 里的单文件版验证算法）
sim:
	gcc -Wall -O2 -o doc/01-simulation/industrial_pipeline4 \
	    doc/01-simulation/industrial_pipeline4.c -lm -lc -lliquid

# 6. 宿主机仿真回归测试（编译 + 运行 + 比对 recovered.png）
simtest:
	bash tools/sim_test.sh

.PHONY: build clean run sim simtest

