async function getJson(url) {
  const res = await fetch(url, { cache: 'no-store' });
  if (!res.ok) throw new Error(`${url} ${res.status}`);
  return res.json();
}

function fmt(value, suffix = '') {
  const n = Number(value || 0);
  return `${n.toFixed(2)}${suffix}`;
}

async function refresh() {
  try {
    const [health, stats, events] = await Promise.all([
      getJson('/api/health'),
      getJson('/api/stats'),
      getJson('/api/events')
    ]);
    document.getElementById('health').textContent = `${health.status} · ${health.backend} · ${health.detector}`;
    document.getElementById('fps').textContent = fmt(stats.fps, ' FPS');
    document.getElementById('latency').textContent = fmt(stats.avg_end_to_end_ms, ' ms');
    document.getElementById('frames').textContent = stats.frame_count || 0;
    document.getElementById('infer').textContent = fmt(stats.avg_inference_ms, ' ms');
    document.getElementById('events-count').textContent = stats.event_count || 0;
    document.getElementById('dropped').textContent = (stats.queues && stats.queues.dropped_frames) || 0;
    const holder = document.getElementById('events');
    holder.innerHTML = '';
    events.slice(0, 12).forEach((event) => {
      const row = document.createElement('div');
      row.className = 'event';
      row.innerHTML = `<strong>${event.class_name}</strong><span>${Number(event.confidence).toFixed(2)}</span><small>${event.timestamp}</small>`;
      holder.appendChild(row);
    });
  } catch (err) {
    document.getElementById('health').textContent = `offline · ${err.message}`;
  }
}

refresh();
setInterval(refresh, 1000);
