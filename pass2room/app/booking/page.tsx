"use client";

import { useState, useEffect } from "react";
import { db } from "@/lib/firebase";
import {
  addDoc,
  collection,
  serverTimestamp,
  onSnapshot,
  query,
  where,
  doc,
  updateDoc,
} from "firebase/firestore";
import { motion } from "framer-motion";

export default function BookingPage() {
  const [studentId, setStudentId] = useState("");
  const [name, setName] = useState("");

  const [room] = useState("A");
  const [date, setDate] = useState("");
  const [startTime, setStartTime] = useState("08:00");
  const [endTime, setEndTime] = useState("09:00");

  const [bookings, setBookings] = useState<any[]>([]);
  const [winner, setWinner] = useState<any>(null);

  const [spinIndex, setSpinIndex] = useState(0);
  const [isSpinning, setIsSpinning] = useState(false);

  const [sessionId, setSessionId] = useState("");

  // โหลด studentId
  useEffect(() => {
    const saved = localStorage.getItem("studentId");
    if (saved) setStudentId(saved);
  }, []);

  // สร้าง session
  useEffect(() => {
    if (!date) return;

    setSessionId(`${room}_${date}`);

    // 🔥 RESET winner ทุกครั้งที่เปลี่ยน slot
    setWinner(null);
  }, [date, startTime, endTime]);

  // realtime
  useEffect(() => {
    if (!sessionId) return;

    const q = query(
      collection(db, "bookings"),
      where("sessionId", "==", sessionId)
    );

    const unsub = onSnapshot(q, (snap) => {
      const list = snap.docs.map((d) => ({
        id: d.id,
        ...(d.data() as any),
      }));

      setBookings(list);

      // 🔥 หา winner เฉพาะ slot ปัจจุบัน
      const currentSlot = list.filter(
        (b) =>
          b.startTime === startTime &&
          b.endTime === endTime
      );

      const win = currentSlot.find(
        (b) => b.status === "approved"
      );

      setWinner(win || null);
    });

    return () => unsub();
  }, [sessionId, startTime, endTime]);

  // 🎰 สุ่ม 3 คน (fix แล้ว)
  useEffect(() => {
    const slot = bookings.filter(
      (b) =>
        b.startTime === startTime &&
        b.endTime === endTime
    );

    const hasWinner = slot.some(
      (b) => b.status === "approved"
    );

    if (slot.length === 3 && !hasWinner && !isSpinning) {
      spin(slot);
    }
  }, [bookings, startTime, endTime]);

  const spin = (group: any[]) => {
    setIsSpinning(true);

    let i = 0;
    const interval = setInterval(() => {
      setSpinIndex((p) => (p + 1) % group.length);
      i++;

      if (i > 20) {
        clearInterval(interval);
        finish(group);
      }
    }, 80);
  };

  const finish = async (group: any[]) => {
    const rand = Math.floor(Math.random() * group.length);
    const win = group[rand];

    for (let b of group) {
      await updateDoc(doc(db, "bookings", b.id), {
        status: b.id === win.id ? "approved" : "rejected",
      });
    }

    setWinner(win);
    setIsSpinning(false);
  };

  // 🚀 SAVE
  const save = async () => {
    if (!studentId || !name || !date)
      return alert("❗กรอกข้อมูลให้ครบ");

    const toMin = (t: string) => {
      const [h, m] = t.split(":").map(Number);
      return h * 60 + m;
    };

    const start = toMin(startTime);
    const end = toMin(endTime);
    const duration = end - start;

    if (start < 480 || end > 1140)
      return alert("❌ ใช้ได้ 08:00 - 19:00");

    if (start >= end)
      return alert("❌ เวลาไม่ถูกต้อง");

    if (duration > 180)
      return alert("❌ ไม่เกิน 3 ชั่วโมง");

    const slot = bookings.filter(
      (b) =>
        b.startTime === startTime &&
        b.endTime === endTime
    );

    if (slot.length >= 3)
      return alert("❌ เต็มแล้ว");

    const duplicate = slot.some(
      (b) => b.studentId === studentId
    );

    if (duplicate)
      return alert("❌ ลงแล้ว");

    const conflict = bookings.some((b) => {
      if (b.status !== "approved") return false;

      const bStart = toMin(b.startTime);
      const bEnd = toMin(b.endTime);

      return !(end <= bStart || start >= bEnd);
    });

    if (conflict)
      return alert("❌ เวลานี้ถูกจองแล้ว");

    await addDoc(collection(db, "bookings"), {
      sessionId,
      studentId,
      name,
      room,
      date,
      startTime,
      endTime,
      status: "pending",
      createdAt: serverTimestamp(),
    });

    localStorage.setItem("studentId", studentId);
    setName("");
  };

  const slot = bookings.filter(
    (b) =>
      b.startTime === startTime &&
      b.endTime === endTime
  );

  return (
    <div className="p-6 max-w-xl mx-auto space-y-6">

      <h1 className="text-3xl font-bold text-center">
        🏫 PASS2ROOM
      </h1>

      {/* FORM */}
      <div className="bg-white p-4 rounded-xl shadow space-y-2">
        <input
          placeholder="🎓 Student ID"
          value={studentId}
          onChange={(e) => setStudentId(e.target.value)}
          className="w-full p-2 border rounded"
        />

        <input
          placeholder="👤 Name"
          value={name}
          onChange={(e) => setName(e.target.value)}
          className="w-full p-2 border rounded"
        />

        <input
          type="date"
          value={date}
          onChange={(e) => setDate(e.target.value)}
          className="w-full p-2 border rounded"
        />

        <div className="grid grid-cols-2 gap-2">
          <input
            type="time"
            value={startTime}
            onChange={(e) => setStartTime(e.target.value)}
            className="border p-2 rounded"
          />
          <input
            type="time"
            value={endTime}
            onChange={(e) => setEndTime(e.target.value)}
            className="border p-2 rounded"
          />
        </div>

        <button
          onClick={save}
          className="w-full bg-black text-white p-2 rounded"
        >
          🚀 Book
        </button>

        <p className="text-center text-lg">
          👥 {slot.length}/3
        </p>
      </div>

      {/* 🎰 DRAW */}
      <div className="bg-purple-500 text-white p-5 rounded-xl text-center">

        <h2>🎰 Lucky Draw</h2>

        <motion.div className="bg-black py-4 mt-2 rounded text-xl">
          {bookings[spinIndex]?.name || "---"}
        </motion.div>

        {winner && (
          <p className="mt-3 text-xl">
            🏆 {winner.name} ({winner.studentId})
          </p>
        )}

        {studentId && winner && (
          <>
            {winner.studentId === studentId ? (
              <>
                <p className="text-green-200 text-xl">🏆 YOU WIN</p>

                <button
                  onClick={async () => {
                    await fetch("/api/unlock", {
                      method: "POST",
                      headers: {
                        "Content-Type": "application/json",
                      },
                      body: JSON.stringify({
                        studentId,
                        room: "A",
                      }),
                    });

                    alert("🚪 Door Opening...");
                  }}
                  className="mt-2 bg-green-600 px-4 py-2 rounded"
                >
                  🔓 Unlock
                </button>
              </>
            ) : (
              <p className="text-red-200 text-xl">❌ YOU LOSE</p>
            )}
          </>
        )}
      </div>

      {/* 📊 ห้องถูกจอง */}
      <div className="bg-green-100 p-3 rounded">
        <p className="font-bold">📊 ห้องถูกจองแล้ว</p>

        {bookings
          .filter((b) => b.status === "approved")
          .map((b) => (
            <div key={b.id}>
              🏆 {b.name} ({b.studentId})<br />
              ⏰ {b.startTime}-{b.endTime}
            </div>
          ))}
      </div>

    </div>
  );
}