include ../toolchain/toolchain.mk

TARGET = pluto_spectrum_analyzer

# 使用 ?= 允许命令行传参覆盖。若命令行未指定，则使用默认值
# make run FREQ=100.0 SAMPLE=4
IP     ?= 192.168.2.10
PORT   ?= 1234
FREQ   ?= 93
SAMPLE ?= 10

# 4. 编译规则
build:
	@# 联合编译当前目录下的 kiss_fft 基础组件
	$(CC) $(TARGET).c kiss_fft.c -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -rf $(TARGET)

run:
	@sshpass -p '$(SSH_PASS)' scp -O $(SSH_OPTS) $(TARGET) root@pluto.local:~/
	@echo "------------------------------------------------"
	@echo " |  __ \| |     | |       / ___|                            | |"
	@echo " | |__) | |_   _| |_ ___  \___ \  _ __   ___  ___ |_   _ __ | |"
	@echo " |  ___/| | | | | __/ _ \  ___) || '_ \ / _ \/ __| __| '__| | |"
	@echo " | |    | | |_| | || (_) ||____/ | |_) |  __/ (__ | |_| |   |_|"
	@echo " |_|    |_|\__,_|\__\___/        | .__/ \___|\___| \__|_|   (_)"
	@echo "                                 | |"
	@echo "                                 |_|"
	@echo "Tips: On your PC, use Python or GNU Radio to receive"
	@echo "1024 float points via UDP port 1234 to plot waterfall."
	@echo "python pluto_spec_viewer.py -s $(SAMPLE) -i \"$(IP)\" -p $(PORT)"
	@echo "------------------------------------------------"
	@sshpass -p '$(SSH_PASS)' ssh $(SSH_OPTS) -t root@pluto.local "chmod +x ~/$(TARGET); ./$(TARGET) -f $(FREQ) -s $(SAMPLE) -i $(IP) -p $(PORT)"
