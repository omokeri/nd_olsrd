OLSRD_PLUGIN =	false
PLUGIN_NAME =	olsrd_info
PLUGIN_VER =	0.0

TOPDIR=../..
include $(TOPDIR)/Makefile.inc

default_target: $(PLUGIN_FULLNAME)

$(PLUGIN_FULLNAME): $(OBJS)

install:

uninstall:

clean:
	rm -f $(OBJS) $(SRCS:%.c=%.d) $(PLUGIN_FULLNAME)
