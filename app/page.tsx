"use client";
import { motion} from "framer-motion";

export default function Home() {
  return (
    <div className="flex min-h-screen items-center justify-center bg-zinc-50 font-sans dark:bg-black">
      <section className="min-h-screen w-full flex items-center justify-end p-12">
        <div className="w-full md:w-1/2 text-right space-y-6">
          <div className="sticky top-1/3">
            <motion.h1 initial={{ opacity: 0, y: 20 }} animate={{ opacity: 1, y: 0 }} transition={{ duration: 0.8 }} className="text-7xl font-black text-white leading-none">
              Welcome to <br /> <span className="text-blue-500">Hazardnode</span>
            </motion.h1>
            <motion.p initial={{ opacity: 0 }} animate={{ opacity: 1 }} transition={{ delay: 0.5, duration: 1 }} className="mt-4 text-xl text-slate-400 font-light">
              niger nigger nigger <br /><br />
              <span className="text-xs uppercase tracking-widest text-blue-400/60 font-bold">Note: This website is a work in progress.</span>
            </motion.p>
          </div>
        </div>
      </section>
      </div>
  );
}
