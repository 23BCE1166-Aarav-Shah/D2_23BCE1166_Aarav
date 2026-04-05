'use strict';

const duplicateEngine = require('./index');

async function run() {
  const targetPath = process.argv[2];
  if (!targetPath) {
    throw new Error('Usage: node example.js <path>');
  }

  const timer = setInterval(() => {
    console.log(`Progress: ${duplicateEngine.getProgress()}%`);
  }, 500);

  try {
    const groups = await duplicateEngine.scan(targetPath);
    clearInterval(timer);
    console.log(JSON.stringify(groups, null, 2));
  } catch (error) {
    clearInterval(timer);
    console.error(error);
    process.exitCode = 1;
  }
}

run();
