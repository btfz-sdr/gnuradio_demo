include ../toolchain/toolchain.mk

TARGET = pluto_ble_adv_tx

# 使用 ?= 允许命令行传参覆盖。若命令行未指定，则使用默认值
# make run CH=37 INTERVAL=20 ATTN=0 SAMPLE=4
# 注意: 换信道请用 CH (信道号)。中心频率和"解白化种子"都由信道号统一决定,
#       只改频率不改信道 -> 标准接收机解不出来（旧的 FREQ 参数已废弃）。
CH       ?= 37
INTERVAL ?= 200
ATTN     ?= 30
SAMPLE   ?= 4

# 4. 编译规则
build:
	$(CC) $(TARGET).c ble_phy.c -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -rf $(TARGET)

run: clean build
	@sshpass -p '$(SSH_PASS)' scp -O $(SSH_OPTS) $(TARGET) root@pluto.local:~/
	@echo "------------------------------------------------"
	@echo " ____  _          _____ _           _ "
	@echo "| __ )| | ___    | ____| |_   _____| |"
	@echo "|  _ \| |/ _ \   |  _| | \ \ / / _ \ |"
	@echo "| |_) | |  __/   | |___| |\ V /  __/ |"
	@echo "|____/|_|\___|   |_____|_| \_/ \___|_|"
	@echo "                                         "
	@echo "------------------------------------------------"
	@sshpass -p '$(SSH_PASS)' ssh $(SSH_OPTS) -t root@pluto.local "chmod +x ~/$(TARGET); ./$(TARGET) -c $(CH) -i $(INTERVAL) -a $(ATTN) -s $(SAMPLE)"
