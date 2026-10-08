# Makefile — switch51.2t 模型(SystemC)  [2026-10-08 建 / M0]
# ⚠ SYSTEMC_HOME 可用环境变量覆盖(交付件不绑死本机路径)
SYSTEMC_HOME ?= /opt/tools/systemc2.34
CXX      := g++
CXXFLAGS := -std=c++14 -O1 -g -Wall -I$(SYSTEMC_HOME)/include -Iport -Icore -Itop -Itb
LDFLAGS  := -L$(SYSTEMC_HOME)/lib-linux64 -lsystemc -Wl,-rpath,$(SYSTEMC_HOME)/lib-linux64

LIB_OBJS := port/mac_tlm.o

all: tb_sw_bringup2 tb_sw_bringup64 tb_sw_l2_uni tb_sw_l2_flood tb_sw_l2_mcast tb_sw_pfc tb_sw_ecn \
     tb_sw_scale64 tb_sw_stress_q tb_sw_vlan tb_mac_loopback tb_mac_lane800

tb_sw_bringup2: $(LIB_OBJS) tb/tb_sw_bringup2.o
	$(CXX) -o $@ $^ $(LDFLAGS)
tb/tb_sw_bringup2.o: tb/tb_sw_bringup.cpp top/switch_top.h core/sw_core.h port/mac_tlm.h
	$(CXX) $(CXXFLAGS) -DSW_N_PORTS=2 -c -o $@ $<

tb_sw_bringup64: $(LIB_OBJS) tb/tb_sw_bringup64.o
	$(CXX) -o $@ $^ $(LDFLAGS)
tb/tb_sw_bringup64.o: tb/tb_sw_bringup.cpp top/switch_top.h core/sw_core.h port/mac_tlm.h
	$(CXX) $(CXXFLAGS) -DSW_N_PORTS=64 -c -o $@ $<

# ---- M1:L2 转发面三台(N 在 TB 内定:3/4/4)----
TB_L2_DEPS = tb/sw_l2_common.h top/switch_top.h core/sw_core.h port/mac_tlm.h

tb_sw_l2_uni: $(LIB_OBJS) tb/tb_sw_l2_uni.o
	$(CXX) -o $@ $^ $(LDFLAGS)
tb/tb_sw_l2_uni.o: tb/tb_sw_l2_uni.cpp $(TB_L2_DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

tb_sw_l2_flood: $(LIB_OBJS) tb/tb_sw_l2_flood.o
	$(CXX) -o $@ $^ $(LDFLAGS)
tb/tb_sw_l2_flood.o: tb/tb_sw_l2_flood.cpp $(TB_L2_DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

tb_sw_l2_mcast: $(LIB_OBJS) tb/tb_sw_l2_mcast.o
	$(CXX) -o $@ $^ $(LDFLAGS)
tb/tb_sw_l2_mcast.o: tb/tb_sw_l2_mcast.cpp $(TB_L2_DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ---- M2:无损(PFC)四模式台(单二进制:xoff/gate/lossless/off)----
tb_sw_pfc: $(LIB_OBJS) tb/tb_sw_pfc.o
	$(CXX) -o $@ $^ $(LDFLAGS)
tb/tb_sw_pfc.o: tb/tb_sw_pfc.cpp $(TB_L2_DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ---- M3:ECN/WRED 两模式台(单二进制:mark/count)----
tb_sw_ecn: $(LIB_OBJS) tb/tb_sw_ecn.o
	$(CXX) -o $@ $^ $(LDFLAGS)
tb/tb_sw_ecn.o: tb/tb_sw_ecn.cpp $(TB_L2_DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ---- M4:规模/压力台 + ★L5 护城河(800G 线两份台**逐字节平移**,只依赖 mac_tlm.h)----
tb_sw_scale64: $(LIB_OBJS) tb/tb_sw_scale64.o
	$(CXX) -o $@ $^ $(LDFLAGS)
tb/tb_sw_scale64.o: tb/tb_sw_scale64.cpp $(TB_L2_DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

tb_sw_stress_q: $(LIB_OBJS) tb/tb_sw_stress_q.o
	$(CXX) -o $@ $^ $(LDFLAGS)
tb/tb_sw_stress_q.o: tb/tb_sw_stress_q.cpp $(TB_L2_DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

tb_sw_vlan: $(LIB_OBJS) tb/tb_sw_vlan.o
	$(CXX) -o $@ $^ $(LDFLAGS)
tb/tb_sw_vlan.o: tb/tb_sw_vlan.cpp $(TB_L2_DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

tb_mac_loopback: $(LIB_OBJS) tb/tb_mac_loopback.o
	$(CXX) -o $@ $^ $(LDFLAGS)
tb/tb_mac_loopback.o: tb/tb_mac_loopback.cpp port/mac_tlm.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

tb_mac_lane800: $(LIB_OBJS) tb/tb_mac_lane800.o
	$(CXX) -o $@ $^ $(LDFLAGS)
tb/tb_mac_lane800.o: tb/tb_mac_lane800.cpp port/mac_tlm.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

port/mac_tlm.o: port/mac_tlm.cpp port/mac_tlm.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

regress: all
	sh ./regress.sh

clean:
	rm -f tb_sw_bringup2 tb_sw_bringup64 tb_sw_l2_uni tb_sw_l2_flood tb_sw_l2_mcast tb_sw_pfc tb_sw_ecn \
      tb_sw_scale64 tb_sw_stress_q tb_sw_vlan tb_mac_loopback tb_mac_lane800 \
	      tb/tb_sw_bringup2.o tb/tb_sw_bringup64.o tb/tb_sw_l2_uni.o tb/tb_sw_l2_flood.o \
	      tb/tb_sw_l2_mcast.o tb/tb_sw_pfc.o tb/tb_sw_ecn.o \
      tb/tb_sw_scale64.o tb/tb_sw_stress_q.o tb/tb_sw_vlan.o tb/tb_mac_loopback.o tb/tb_mac_lane800.o \
	      $(LIB_OBJS)

.PHONY: all regress clean
