print("hello")


--print_t(package.loaded, 10000)
local i = 0
while i < 1000000000 do
	if i % 200000 == 0 then
		print("i = ", i)
	end
	i = i + 1
end
--os.execute(" sleep " .. 30)

print("world")

return 3, 4