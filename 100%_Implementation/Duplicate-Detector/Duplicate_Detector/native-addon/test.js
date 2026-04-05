const addon = require('./build/Release/duplicate_engine_addon.node');

async function run(){
    console.log("Starting...");
    const res = await addon.scan("/tmp");
    console.log(res);
}

run();
