# RPC 模式通信协议

## 协议
用 zmq 多部分消息包组成一个 RPC 通信协议

每个部分的类型均为字符串

第一个部分均为 routing_id   
第二格部分为角色标签   
第三个部分为请求类型或者响应类型   


### client 会发出的消息包

发起 RPC 请求: uuid | RPC_CLIENT | RPC_REQ | 服务名 | 请求参数  

发起 RPC 服务查询是否存在请求: uuid | RPC_CLIENT | RPC_QUERY | 服务名


### worker 会发出的消息包

发起服务注册请求: uuid | RPC_WORKER | RPC_REG | 服务名1 | 服务名2 | ...

响应 RPC 请求给 broker 端: uuid | RPC_WORKER | RPC_RES | client_id | 服务名 | body

发出心跳包给 broker: uuid | RPC_WORKER | RPC_HB 

发出下线请求: uuid | RPC_WORKER | RPC_UNCONNECT


### broker 会发出的消息包

响应服务注册成功请求: uuid | RPC_BROKER | RPC_SUCCESS 

转发 rpc 响应给 client: uuid | RPC_WORKER | RPC_RES | 服务名 | body



