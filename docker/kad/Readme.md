# Kad docker compose network for debugging

### run and print logs

indefinitely:
```bash
python3 docker/kad/kadnet.py --down && python3 docker/kad/kadnet.py --nodes 100 --build && clear && python3 docker/kad/kadnet.py --logs 
```

5min to file
```bash
python3 docker/kad/kadnet.py --down && python3 docker/kad/kadnet.py --nodes 100 --build && clear && python3 docker/kad/kadnet.py --logs 2>&1 | tee docker/kad/nodes.log & sleep 300 && kill %1; python3 docker/kad/kadnet.py --down
```

### analyze logs
```bash
python3 docker/kad/analyze_log.py docker/kad/nodes.log
```
