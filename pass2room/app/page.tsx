import Link from "next/link";

export default function Home() {
  return (
    <div className="min-h-screen flex justify-center items-center bg-gray-50 p-6">
      <div className="bg-white shadow rounded-2xl p-8 max-w-lg w-full text-center">
        <h1 className="text-3xl font-bold mb-2">PASS2ROOM</h1>

        <p className="text-gray-500 mb-6">
          ระบบจองห้องและควบคุมการเข้าใช้งาน / Smart Room Booking & Access
        </p>

        <Link
          href="/booking"
          className="block w-full bg-black text-white p-3 rounded-xl font-semibold hover:bg-gray-800"
        >
          Booking / จองห้อง
        </Link>
      </div>
    </div>
  );
}