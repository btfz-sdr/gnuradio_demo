include ../toolchain/toolchain.mk

TARGET = ad9361-iiostream

# 编译规则
build:
	$(CC) $(TARGET).c -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	@# 可选：减小二进制体积，方便传输到 PlutoSDR
	$(STRIP) $(TARGET)

clean:
	rm -rf $(TARGET)

run:
	@echo "手动执行 ./ad9361-iiostream"
	@sshpass -p '$(SSH_PASS)' scp -O $(SSH_OPTS) $(TARGET) root@pluto.local:~/
	@sshpass -p '$(SSH_PASS)' ssh $(SSH_OPTS) -t root@pluto.local "chmod +x ~/$(TARGET); /bin/sh"

