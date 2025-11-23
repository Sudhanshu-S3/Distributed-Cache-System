# Distributed-Cache-System


### Architecture Design

```mermaid
---
config:
  theme: neutral
  themeVariables:
    fontFamily: Inter, Arial
    fontSize: 14px
    primaryColor: '#eef2ff'
    primaryBorderColor: '#4f46e5'
    primaryTextColor: '#1f2937'
  layout: fixed
---

flowchart LR
 subgraph PH6["Phase 6: Clients & Admin"]
        CLI["Admin CLI<br>cachectl"]
        SDK["Client SDK<br>C++ Lib"]
  end
 subgraph PH3["Phase 3: Networking Layer"]
        ASIO["ASIO Reactor<br>TCP Server"]
        PROTO["Protocol Parser<br>Binary Framing"]
  end
 subgraph PH5["Phase 5: Cluster & Routing"]
        MEM["Membership<br>Gossip / SWIM"]
        RING["Consistent Hash Ring<br>Node Selection"]
  end
 subgraph PH4["Phase 4: Reliability"]
        REP["Replication Log<br>Primary-Backup"]
  end
 subgraph CORE["Phase 1 & 2: Sharded Core Engine"]
        SHARD((("Shard Controller<br>(Striped Locks)")))
        HT["Fixed Hash Table<br>Open Addressing"]
        ARENA["Memory Arena<br>Fixed Allocator"]
        TTL["TTL Wheel<br>Expiration"]
  end
 subgraph OBS["Phase 6: Telemetry"]
        MET["Metrics<br>Prometheus"]
  end
    CLI -- HTTP/TCP --> ASIO
    SDK -- TCP --> ASIO
    ASIO -- Raw Bytes --> PROTO
    PROTO -- Command Object --> RING
    MEM -- Node Map Update --> RING
    RING -- Route to Local Shard --> SHARD
    RING -. Forward to Remote .-> ASIO
    SHARD -- Lock & Access --> HT
    HT -- Allocate Data --> ARENA
    TTL -- Evict Keys --> HT
    SHARD -- Async Write --> REP
    SHARD -. Count Ops .-> MET
    ASIO -. Latency .-> MET

     CLI:::client
     SDK:::client
     ASIO:::net
     PROTO:::net
     MEM:::cluster
     RING:::cluster
     REP:::cluster
     SHARD:::controller
     HT:::core
     ARENA:::core
     TTL:::core
     MET:::obs

    classDef client fill:#dceafe,stroke:#1d4ed8,stroke-width:2px,color:#1e293b
    classDef net fill:#e0e7ff,stroke:#4338ca,stroke-width:2px,color:#1e293b
    classDef cluster fill:#ffffff,stroke:#2563eb,stroke-width:2px,color:#1e293b
    classDef core fill:#d1fae5,stroke:#047857,stroke-width:2px,color:#0f172a
    classDef obs fill:#f5f5f4,stroke:#57534e,stroke-width:2px,color:#1e293b
    classDef controller fill:#cffafe,stroke:#0e7490,stroke-width:3px,color:#1f2937,font-size:16px
```
