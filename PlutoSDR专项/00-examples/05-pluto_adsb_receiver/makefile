include ../toolchain/toolchain.mk

TARGET = pluto_adsb_receiver

# 使用 ?= 允许命令行传参覆盖。若命令行未指定，则使用默认值
# make run FREQ=1090 SAMPLE=10
IP     ?= 192.168.2.10
PORT   ?= 1234
FREQ   ?= 1090
SAMPLE ?= 4

# 4. 编译规则
build:
	$(CC) $(TARGET).c -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -rf $(TARGET)

run:
	@sshpass -p '$(SSH_PASS)' scp -O $(SSH_OPTS) $(TARGET) root@pluto.local:~/
	@echo "------------------------------------------------"
	@echo "   ____  _   _ ____  _____ ____     _           _   "
	@echo "  |  _ \\| | | |  _ \\| ____|  _ \\   / \\   _ __ | |_ "
	@echo "  | |_) | | | | | | |  _| | |_) | / _ \\ | '_ \\| __|"
	@echo "  |  __/| |_| | |_| | |___|  _ < / ___ \\| | | | |_ "
	@echo "  |_|    \\___/|____/|_____|_| \\_/_/   \\_\\_| |_|\\__|"
	@echo "                                 by beautifulzzzz"
	@echo "Tips: On your PC, run Python to receive decoded"
	@echo "ADS-B messages via UDP port $(PORT):"
	@echo "python pluto_adsb_viewer.py --ip $(IP) --port $(PORT)"
	@echo "------------------------------------------------"
	@sshpass -p '$(SSH_PASS)' ssh $(SSH_OPTS) -t root@pluto.local "chmod +x ~/$(TARGET); ./$(TARGET) -f $(FREQ) -s $(SAMPLE) -i $(IP) -p $(PORT)"
