include ../toolchain/toolchain.mk

TARGET = pluto_fm_radio

# 编译规则
build:
	$(CC) $(TARGET).c -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	@# 可选：减小二进制体积，方便传输到 PlutoSDR
	$(STRIP) $(TARGET)

clean:
	rm -rf $(TARGET)

run:
	@sshpass -p '$(SSH_PASS)' scp -O $(SSH_OPTS) $(TARGET) root@pluto.local:~/
	@echo "------------------------------------------------"
	@echo "Tips: On your PC, run this to hear sound:"
	@echo "ffplay -f s16le -ar 48000  udp://@:1234"
	@echo "ffplay -nodisp -f s16le -ar 48000 -ch_layout mono -fflags nobuffer -flags low_delay udp://192.168.2.10:1234"
	@echo "ffplay -nodisp -f s16le -ar 48000 -ch_layout mono -framedrop -async 1 -fflags nobuffer udp://192.168.2.10:1234"
	@echo "------------------------------------------------"
	@sshpass -p '$(SSH_PASS)' ssh $(SSH_OPTS) -t root@pluto.local "chmod +x ~/$(TARGET); ./$(TARGET)"
