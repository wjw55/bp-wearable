const express = require('express');
const cors = require('cors');
const { WebSocketServer } = require('ws');
const http = require('http');

const app = express();
app.use(cors());
app.use(express.json());

const server = http.createServer(app);
const wss = new WebSocketServer({ server });

// Keep last 60 readings for history
const MAX_HISTORY = 60;
let history = [];
let latestReading = null;

app.get('/health', (req, res) => {
  res.json({ ok: true, service: 'bp-wearable-backend' });
});

// Broadcast to all connected WebSocket clients
function broadcast(data) {
  const msg = JSON.stringify(data);
  wss.clients.forEach(client => {
    if (client.readyState === 1) client.send(msg);
  });
}

// ESP32 posts here
app.post('/api/heartrate', (req, res) => {
  const { bpm, avgBpm, ir, systolic, diastolic, fingerDetected, timestamp } = req.body;

  const reading = {
    bpm: Math.round(bpm),
    avgBpm: Math.round(avgBpm),
    ir,
    systolic,
    diastolic,
    fingerDetected,
    timestamp: Date.now()
  };

  latestReading = reading;

  if (fingerDetected && avgBpm > 0) {
    history.push({ time: reading.timestamp, bpm: reading.avgBpm, systolic: reading.systolic, diastolic: reading.diastolic });
    if (history.length > MAX_HISTORY) history.shift();
  }

  broadcast({ type: 'reading', reading, history });
  res.json({ ok: true });
});

// New WebSocket client — send current state immediately
wss.on('connection', ws => {
  console.log('Dashboard connected');
  if (latestReading) {
    ws.send(JSON.stringify({ type: 'reading', reading: latestReading, history }));
  }
});

server.listen(3001, '0.0.0.0', () => console.log('Server running on 0.0.0.0:3001'));

