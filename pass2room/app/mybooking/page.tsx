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

  const [room, setRoom] = useState("A");
  const [date, setDate] = useState("");
  const [startTime, setStartTime] = useState("08:00");
  const [endTime, setEndTime] = useState("09:00");

  const [bookings, setBookings] = useState<any[]>([]);
  const [winner, setWinner] = useState<any>(null);

  const [spinIndex, setSpinIndex] = useState(0);
  const [isSpinning, setIsSpinning] = useState(false);

  const [sessionId, setSessionId] = useState("");

  useEffect(() => {
    const saved = localStorage.getItem("studentId");
    if (saved) setStudentId(saved);
  }, []);

  useEffect(() => {
    if (!date) return;
    setSessionId(`${room}_${date}`);
  }, [room, date]);

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

      const win = list.find((b) => b.status === "approved");
      if (win) setWinner(win);
    });

    return () => unsub();
  }, [sessionId]);

  // 🎰 สุ่ม (3 คน)
  useEffect(() => {
    const currentSlot = bookings.filter(
      (b) =>
        b.startTime === startTime &&
        b.endTime === endTime
    );

    const hasWinner = currentSlot.find(
      (b) => b.status === "approved"
    );

    if (currentSlot.length === 3 && !hasWinner && !isSpinning) {
      spin(currentSlot);
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
      return alert("❌ ใช้ได้ 08:00-19:00");

    if (start >= end)
      return alert("❌ เวลาไม่ถูกต้อง");

    if (duration > 180)
      return alert("❌ ไม่เกิน 3 ชั่วโมง");

    const sameSlot = bookings.filter(
      (b) =>
        b.startTime === startTime &&
        b.endTime === endTime
    );

    if (sameSlot.length >= 3)
      return alert("❌ ช่วงเวลานี้เต็มแล้ว");

    const duplicateInSlot = sameSlot.some(
      (b) => b.studentId === studentId
    );

    if (duplicateInSlot)
      return alert("❌ คุณลงช่วงเวลานี้แล้ว");

    const alreadyWinner = bookings.some(
      (b) =>
        b.studentId === studentId &&
        b.status === "approved"
    );

    if (alreadyWinner)
      return alert("❌ คุณชนะแล้ว");

    const conflict = bookings.some((b) => {
      if (b.status !== "approved") return false;

      const bStart = toMin(b.startTime);
      const bEnd = toMin(b.endTime);

      return !(end <= bStart || start >= bEnd);
    });

    if (conflict)
      return alert("❌ เวลานี้ถูกจองแล้ว");

    localStorage.setItem("studentId", studentId);

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

    setName("");
  };

  const sameSlot = bookings.filter(
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

        <p className="text-center">👥 {sameSlot.length}/3</p>
      </div>

      {/* 🎰 LUCKY DRAW */}
      <div className="bg-white p-4 rounded-xl shadow text-center">
        <h2 className="text-xl font-bold">🎲 Lucky Draw</h2>

        <motion.div className="bg-black text-white py-4 rounded mt-2 text-lg">
          {bookings[spinIndex]?.name || "---"}
        </motion.div>

        {winner && (
          <div className="mt-4 text-yellow-500 font-bold text-2xl">
            🏆 {winner.name}
          </div>
        )}

        {studentId && winner && (
          <div className="mt-3 font-bold text-lg">
            {winner.studentId === studentId ? (
              <p className="text-green-600">🏆 YOU WIN</p>
            ) : (
              <p className="text-red-500">❌ YOU LOSE</p>
            )}
          </div>
        )}
      </div>

      {/* 📋 My Booking */}
      <div className="bg-white p-3 rounded shadow">
        <p className="font-bold">📋 My Booking</p>

        {bookings
          .filter((b) => b.studentId === studentId)
          .map((b) => (
            <div key={b.id} className="border p-2 mt-2 rounded">
              <p>👤 {b.name}</p>
              <p>🎓 {b.studentId}</p>
              <p>⏰ {b.startTime} - {b.endTime}</p>
              <p>📅 {b.date}</p>
              <p>
                {b.status === "approved"
                  ? "🏆 WIN"
                  : b.status === "rejected"
                  ? "❌ LOSE"
                  : "⏳ PENDING"}
              </p>
            </div>
          ))}
      </div>

      {/* 📊 ห้องถูกจองแล้ว */}
      <div className="bg-green-100 p-3 rounded">
        <p className="font-bold text-lg">📊 ห้องถูกจองแล้ว</p>

        {bookings.filter(b => b.status === "approved").length === 0 && (
          <p>ยังไม่มีผู้ชนะ</p>
        )}

        {bookings
          .filter((b) => b.status === "approved")
          .map((b) => (
            <div key={b.id} className="bg-white p-3 mt-2 rounded shadow">
              <p className="font-bold text-green-700">
                🏆 {b.name}
              </p>
              <p>🎓 {b.studentId}</p>
              <p>⏰ {b.startTime} - {b.endTime}</p>
              <p>📅 {b.date}</p>
            </div>
          ))}
      </div>

    </div>
  );
}