import { initializeApp, getApps } from "firebase/app";
import { getFirestore } from "firebase/firestore";

const firebaseConfig = {
  apiKey: "AIzaSyBo6SQeSHQlVCgPp9kIE9tYH5N4OPCzJV8",
  authDomain: "pass2room.firebaseapp.com",
  projectId: "pass2room",
  storageBucket: "pass2room.firebasestorage.app",
  messagingSenderId: "1027809309730",
  appId: "1:1027809309730:web:93a9444d19e3317a40c67b",
  measurementId: "G-49B0VKP3H4",
};

const app = getApps().length ? getApps()[0] : initializeApp(firebaseConfig);

export const db = getFirestore(app);