// Max's [js] / [jsui] run ES5: reject anything newer.   node max/check_es5.js <files...>
const acorn = require("acorn");
const fs = require("fs");
let bad = 0;
for (const f of process.argv.slice(2))
{
    try { acorn.parse(fs.readFileSync(f, "utf8"), { ecmaVersion: 5 }); }
    catch (e) { console.log("NOT ES5:", f, e.message); bad++; }
}
console.log(process.argv.length - 2, "scripts,", bad ? bad + " not ES5" : "all ES5");
process.exit(bad ? 1 : 0);
