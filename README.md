#sysbak-admin

硬盘/分区备份还原库

##ext/2/3/4 文件系统

###备份分区到分区
```sysbak_extfs_ptf_async```

###备份分区到文件
```sysbak_extfs_ptf_async```

###恢复备份
```sysbak_extfs_restore_async```

##test 使用说明
```test目录下是测试历程，make 后可直接使用test.ini是测试配置文件，测试时需要修改选项,(使用root权限测试)```

##编译
```meson build -Dprefix=/usr```

```ninja -C build```

```sudo ninja -C build install```
