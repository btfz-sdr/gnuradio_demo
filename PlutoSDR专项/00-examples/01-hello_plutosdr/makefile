include ../toolchain/toolchain.mk

TARGET = hello_plutosdr

# 编译规则
build:
	$(CC) $(TARGET).c -o $(TARGET) 
	@# 可选：减小二进制体积，方便传输到 PlutoSDR
	$(STRIP) $(TARGET)

clean:
	rm -rf $(TARGET)

run:
	@sshpass -p '$(SSH_PASS)' scp -O $(SSH_OPTS) $(TARGET) root@pluto.local:~/
	@sshpass -p '$(SSH_PASS)' ssh $(SSH_OPTS) -t root@pluto.local "chmod +x ~/$(TARGET); ./$(TARGET)"
