
```
[02:04:56.732164][Debug][handle_rtnl][#418]nlmsg len 80
[02:04:56.732234][Debug][handle_rtnl][#429]nlmsg[0], RTM_NEWADDR
[02:04:56.732260][Debug][handle_rtnl][#432]ifa_family: IPv6, ifa_prefixlen: 64, ifa_flags: 0x0, ifa_scope: UNIVERSE (0x0)
[02:04:56.732291][Debug][handle_rtnl][#467]IFA_ADDRESS IPv6: 2001:b011:381b:d095:6e30:2aff:fe08:8b9c/64
```

ip l set wlan0 down; ip l set wlan0 up

```
[02:07:42.812140][Debug][handle_rtnl][#418]nlmsg len 80
[02:07:42.812211][Debug][handle_rtnl][#429]nlmsg[0], RTM_NEWADDR
[02:07:42.812245][Debug][handle_rtnl][#432]ifa_family: IPv6, ifa_prefixlen: 64, ifa_flags: 0x80, ifa_scope: LINK (0xfd)
[02:07:42.812274][Debug][handle_rtnl][#467]IFA_ADDRESS IPv6: fe80::6e30:2aff:fe08:8b9c/64
```

udhcpc -i wlan0 -q
ifconfig wlan0 192.168.31.111 broadcast 192.168.31.255 netmask 255.255.255.0

```
[02:10:02.035194][Debug][handle_rtnl][#418]nlmsg len 88
[02:10:02.035294][Debug][handle_rtnl][#511]nlmsg[0], RTM_DELADDR flag: 0x10008 (LOOPBACK, LOWER_UP)
[02:10:02.243500][Debug][handle_rtnl][#418]nlmsg len 88
[02:10:02.243590][Debug][handle_rtnl][#429]nlmsg[0], RTM_NEWADDR
[02:10:02.243628][Debug][handle_rtnl][#432]ifa_family: IPv4, ifa_prefixlen: 24, ifa_flags: 0x80, ifa_scope: UNIVERSE (0x0)
[02:10:02.243657][Debug][handle_rtnl][#474]IFA_ADDRESS IPv4: 192.168.31.111/24
[02:10:02.243671][Debug][handle_rtnl][#451]IFA_LOCAL IPv4: 192.168.31.111/24
[02:10:02.243683][Debug][handle_rtnl][#443]IFA_LABEL: wlan0
```


zcip -q -v wlan0 /usr/share/zcip/default.script

```
[00:03:05.629587][Debug][handle_rtnl][#419]nlmsg len 88
[00:03:05.629675][Debug][handle_rtnl][#431]nlmsg[0], RTM_NEWADDR
[00:03:05.629710][Debug][handle_rtnl][#433]ifa_family: IPv4, ifa_prefixlen: 16, ifa_flags: 0x80, ifa_scope: LINK (0xfd)
[00:03:05.629824][Debug][handle_rtnl][#475]IFA_ADDRESS IPv4: 169.254.99.122/16
[00:03:05.629970][Debug][handle_rtnl][#452]IFA_LOCAL IPv4: 169.254.99.122/16  
[00:03:05.630083][Debug][handle_rtnl][#444]IFA_LABEL: wlan0
```