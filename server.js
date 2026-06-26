const express = require('express');
const path = require('path');
const fs = require('fs/promises');

const app = express();
const PORT = process.env.PORT || 8000;
const DATA_DIR = path.join(__dirname, 'data');
const DB_FILE = path.join(DATA_DIR, 'db.json');

const DEFAULT_ROSTER = [
  { id: '241100150', name: 'Yahya Shujaa', time: '-', status: 'absent', note: '-' },
  { id: '231100147', name: 'Rayan Alqahtani', time: '-', status: 'absent', note: '-' },
  { id: '241100519', name: 'Mazen Almunjad', time: '-', status: 'absent', note: '-' }
];

app.use(express.json());
app.use(express.static(__dirname));

function nowTime() {
  return new Date().toTimeString().slice(0, 8);
}

async function ensureDb() {
  await fs.mkdir(DATA_DIR, { recursive: true });
  try {
    await fs.access(DB_FILE);
  } catch {
    await fs.writeFile(DB_FILE, JSON.stringify({ lectures: [], attendance: {} }, null, 2), 'utf8');
  }
}

async function readDb() {
  await ensureDb();
  const raw = await fs.readFile(DB_FILE, 'utf8');
  return JSON.parse(raw || '{"lectures":[],"attendance":{}}');
}

async function writeDb(db) {
  await fs.writeFile(DB_FILE, JSON.stringify(db, null, 2), 'utf8');
}

function buildLectureId() {
  return `${Date.now()}-${Math.floor(Math.random() * 100000)}`;
}

app.get('/api/health', (_req, res) => {
  res.json({ ok: true });
});

app.get('/api/lectures', async (req, res) => {
  const db = await readDb();
  const subject = req.query.subject || '';
  const doctor = req.query.doctor || '';

  const lectures = db.lectures.filter((l) => {
    const sameSubject = subject ? l.subject === subject : true;
    const sameDoctor = doctor ? l.doctor === doctor : true;
    return sameSubject && sameDoctor;
  });

  res.json(lectures);
});

app.get('/api/lectures/:id', async (req, res) => {
  const db = await readDb();
  const lecture = db.lectures.find((l) => l.id === req.params.id);
  if (!lecture) {
    res.status(404).json({ message: 'Lecture not found.' });
    return;
  }
  res.json(lecture);
});

app.post('/api/lectures', async (req, res) => {
  const { subject, doctor, roomNumber, startTime, endTime, password } = req.body || {};

  if (!subject || !doctor || !roomNumber || !startTime || !endTime || !password) {
    res.status(400).json({ message: 'Missing required fields.' });
    return;
  }

  const db = await readDb();
  const lecture = {
    id: buildLectureId(),
    subject,
    doctor,
    roomNumber,
    startTime,
    endTime,
    password,
    createdAt: new Date().toISOString()
  };

  db.lectures.push(lecture);
  db.attendance[lecture.id] = DEFAULT_ROSTER.map((student) => ({ ...student }));
  await writeDb(db);

  res.status(201).json(lecture);
});

app.delete('/api/lectures/:id', async (req, res) => {
  const id = req.params.id;
  const db = await readDb();
  const before = db.lectures.length;
  db.lectures = db.lectures.filter((l) => l.id !== id);

  if (db.lectures.length === before) {
    res.status(404).json({ message: 'Lecture not found.' });
    return;
  }

  delete db.attendance[id];
  await writeDb(db);
  res.json({ ok: true });
});

app.get('/api/attendance', async (req, res) => {
  const lectureId = req.query.lectureId;
  if (!lectureId) {
    res.status(400).json({ message: 'lectureId is required.' });
    return;
  }

  const db = await readDb();
  if (!db.attendance[lectureId]) {
    db.attendance[lectureId] = DEFAULT_ROSTER.map((student) => ({ ...student }));
    await writeDb(db);
  }

  res.json(db.attendance[lectureId]);
});

app.post('/api/checkin', async (req, res) => {
  const { lectureId, studentId, studentName, password } = req.body || {};
  if (!lectureId || !studentId || !studentName || !password) {
    res.status(400).json({ message: 'Missing required fields.' });
    return;
  }

  const db = await readDb();
  const lecture = db.lectures.find((l) => l.id === lectureId);
  if (!lecture) {
    res.status(404).json({ message: 'Lecture not found.' });
    return;
  }

  if (lecture.password !== password) {
    res.status(401).json({ message: 'Wrong lecture password.' });
    return;
  }

  if (!db.attendance[lectureId]) {
    db.attendance[lectureId] = DEFAULT_ROSTER.map((student) => ({ ...student }));
  }

  const rows = db.attendance[lectureId];
  const idx = rows.findIndex((row) => row.id === studentId);
  const updated = {
    id: studentId,
    name: studentName,
    time: nowTime(),
    status: 'present',
    note: '-'
  };

  if (idx >= 0) {
    rows[idx] = { ...rows[idx], ...updated };
  } else {
    rows.push(updated);
  }

  await writeDb(db);
  res.json({ ok: true, record: updated });
});

app.post('/api/attendance/mark', async (req, res) => {
  const { lectureId, studentId, studentName, status } = req.body || {};
  if (!lectureId || !studentId || !studentName || !status) {
    res.status(400).json({ message: 'Missing required fields.' });
    return;
  }

  const db = await readDb();
  if (!db.attendance[lectureId]) {
    db.attendance[lectureId] = [];
  }

  const rows = db.attendance[lectureId];
  const idx = rows.findIndex((row) => row.id === studentId);
  const normalizedStatus = ['present', 'late', 'absent'].includes(status) ? status : 'absent';

  const updated = {
    id: studentId,
    name: studentName,
    time: normalizedStatus === 'present' ? nowTime() : '-',
    status: normalizedStatus,
    note: '-'
  };

  if (idx >= 0) {
    rows[idx] = { ...rows[idx], ...updated };
  } else {
    rows.push(updated);
  }

  await writeDb(db);
  res.json({ ok: true, record: updated });
});

app.get('/', (_req, res) => {
  res.sendFile(path.join(__dirname, 'indext.html'));
});

app.listen(PORT, () => {
  console.log(`Server running on http://localhost:${PORT}`);
});
