// Shared particle-burst logic for the save celebration. Used by confetti.html
// (the dedicated overlay window) - kept as its own tiny system rather than a
// library so the app stays dependency-free.
const CONFETTI_COLORS = [
  "#ff5c5c", "#ff9f43", "#ffd93d", "#6bcB77", "#4ecdc4",
  "#4d96ff", "#a06cff", "#ff6ec7", "#ffffff",
];

export function createConfetti(canvas) {
  let particles = [];
  let rafId = null;

  function resize() {
    canvas.width = window.innerWidth;
    canvas.height = window.innerHeight;
  }
  resize();
  window.addEventListener("resize", resize);

  function burst(onIdle) {
    const cx = canvas.width / 2;
    const cy = canvas.height * 0.35;
    const count = 140;
    for (let i = 0; i < count; i++) {
      const angle = Math.random() * Math.PI * 2;
      const speed = 120 + Math.random() * 260;
      particles.push({
        x: cx,
        y: cy,
        vx: Math.cos(angle) * speed,
        vy: Math.sin(angle) * speed - 80,
        size: 4 + Math.random() * 5,
        color: CONFETTI_COLORS[(Math.random() * CONFETTI_COLORS.length) | 0],
        rotation: Math.random() * Math.PI * 2,
        spin: (Math.random() - 0.5) * 12,
        life: 0,
        maxLife: 0.9 + Math.random() * 0.6,
      });
    }
    if (!rafId) {
      let last = performance.now();
      const ctx = canvas.getContext("2d");
      const tick = (now) => {
        const dt = Math.min((now - last) / 1000, 0.05);
        last = now;
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        particles = particles.filter((p) => p.life < p.maxLife);
        for (const p of particles) {
          p.life += dt;
          p.vy += 420 * dt;
          p.x += p.vx * dt;
          p.y += p.vy * dt;
          p.rotation += p.spin * dt;
          const t = p.life / p.maxLife;
          const alpha = Math.max(0, 1 - t);
          ctx.save();
          ctx.globalAlpha = alpha;
          ctx.translate(p.x, p.y);
          ctx.rotate(p.rotation);
          ctx.fillStyle = p.color;
          ctx.fillRect(-p.size / 2, -p.size / 2, p.size, p.size * 0.6);
          ctx.restore();
        }
        if (particles.length) {
          rafId = requestAnimationFrame(tick);
        } else {
          rafId = null;
          onIdle?.();
        }
      };
      rafId = requestAnimationFrame(tick);
    }
  }

  function clear() {
    particles = [];
    canvas.getContext("2d").clearRect(0, 0, canvas.width, canvas.height);
  }

  return { burst, clear };
}
