print("hello world")

for v=0,10,2 do
	print("v = "..v)
end

print("end")

for v=1, 10, 1 do
	print("k", v)
end

local i = 0
while true do
	if i % 200000 == 0 then
		print("i = ", i)
		i = 0
	end
	i = i + 1
end
print("wwwwwww")