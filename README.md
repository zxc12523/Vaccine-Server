# Vaccine Server

## This is a simple implement of server and sokcet communication.
## It can deal with multiplexing and lock while another same id login.

### Usage:
#### Read Server
- It can read your current record in register record.
```
$ make
$ ./read_server ${port number}
```

#### Write Server
- It can modify your current record in register record.
```
$ make
$ ./write_server ${port number}
```

#### Login
```
$ telnet localhost $P{port number}
```
