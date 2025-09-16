document.addEventListener('DOMContentLoaded', function() {
    const navLinks = document.querySelectorAll('.sidebar-link');
    const sections = document.querySelectorAll('.content-section');
    const defaultSection = 'overview';

    function showSection(sectionId) {
        sections.forEach(section => section.classList.remove('active'));
        const targetSection = document.getElementById(sectionId);
        if (targetSection) targetSection.classList.add('active');
        navLinks.forEach(link => {
            link.classList.remove('active');
            if (link.dataset.section === sectionId) link.classList.add('active');
        });
        if (sectionId === 'world' && !document.getElementById('quadtree-canvas').hasChildNodes()) {
             setTimeout(runSimulation, 50);
        }
        if (sectionId === 'performance' && !Chart.getChart('performanceChart')) {
            initializePerfChart();
        }
    }

    navLinks.forEach(link => {
        link.addEventListener('click', (e) => {
            e.preventDefault();
            showSection(e.target.closest('a').dataset.section);
        });
    });

    showSection(defaultSection);

    // Architecture module click handlers
    const archInfoBox = document.getElementById('architecture-info');
    if (archInfoBox) {
        const archInfoText = archInfoBox.querySelector('p');
        document.querySelectorAll('.clickable-module').forEach(el => {
            el.addEventListener('click', (e) => {
                e.stopPropagation();
                archInfoText.textContent = el.dataset.info;
                archInfoBox.classList.remove('opacity-0');
            });
        });
    }

    // Component click handlers
    const compInfoBox = document.getElementById('component-info');
    if(compInfoBox) {
        const compInfoText = compInfoBox.querySelector('p');
        document.querySelectorAll('.component-box').forEach(el => {
            el.addEventListener('click', (e) => {
                e.stopPropagation();
                compInfoText.textContent = el.dataset.info;
                compInfoBox.classList.remove('opacity-0');
            });
        });
    }

    // --- Quadtree Simulation ---
    const canvas = document.getElementById('quadtree-canvas');
    const simBtn = document.getElementById('quadtree-sim-btn');
    const playerCountSpan = document.getElementById('player-count');
    let quadtree;

    // Quadtree classes
    class Point {
        constructor(x, y) {
            this.x = x;
            this.y = y;
        }
    }

    class Rectangle {
        constructor(x, y, w, h) {
            this.x = x;
            this.y = y;
            this.w = w;
            this.h = h;
        }
        contains(point) {
            return (point.x >= this.x && point.x < this.x + this.w && point.y >= this.y && point.y < this.y + this.h);
        }
        intersects(range) {
            return !(range.x > this.x + this.w || range.x + range.w < this.x || range.y > this.y + this.h || range.y + range.h < this.y);
        }
    }

    class QuadTree {
        constructor(boundary, capacity) {
            this.boundary = boundary;
            this.capacity = capacity;
            this.points = [];
            this.divided = false;
        }
        subdivide() {
            const { x, y, w, h } = this.boundary;
            const hw = w / 2, hh = h / 2;
            this.northeast = new QuadTree(new Rectangle(x + hw, y, hw, hh), this.capacity);
            this.northwest = new QuadTree(new Rectangle(x, y, hw, hh), this.capacity);
            this.southeast = new QuadTree(new Rectangle(x + hw, y + hh, hw, hh), this.capacity);
            this.southwest = new QuadTree(new Rectangle(x, y + hh, hw, hh), this.capacity);
            this.divided = true;
        }
        insert(point) {
            if (!this.boundary.contains(point)) return false;
            if (this.points.length < this.capacity) {
                this.points.push(point);
                return true;
            }
            if (!this.divided) this.subdivide();
            return this.northeast.insert(point) || this.northwest.insert(point) || this.southeast.insert(point) || this.southwest.insert(point);
        }
        query(range, found = []) {
            if (!this.boundary.intersects(range)) return found;
            for (let p of this.points) {
                if (range.contains(p)) found.push(p);
            }
            if (this.divided) {
                this.northwest.query(range, found);
                this.northeast.query(range, found);
                this.southwest.query(range, found);
                this.southeast.query(range, found);
            }
            return found;
        }
        draw(container) {
            const boundaryDiv = document.createElement('div');
            boundaryDiv.className = 'quadtree-boundary';
            Object.assign(boundaryDiv.style, {
                left: `${this.boundary.x}px`,
                top: `${this.boundary.y}px`,
                width: `${this.boundary.w}px`,
                height: `${this.boundary.h}px`
            });
            container.appendChild(boundaryDiv);
            if (this.divided) {
                this.northeast.draw(container);
                this.northwest.draw(container);
                this.southeast.draw(container);
                this.southwest.draw(container);
            }
        }
    }

    function runSimulation() {
        if (!canvas || !document.getElementById('world').classList.contains('active')) return;
        canvas.innerHTML = '';
        const bounds = new Rectangle(0, 0, canvas.clientWidth, canvas.clientHeight);
        quadtree = new QuadTree(bounds, 4);
        const numPlayers = Math.floor(Math.random() * 150) + 50;
        if(playerCountSpan) playerCountSpan.textContent = numPlayers;
        for (let i = 0; i < numPlayers; i++) {
            const p = new Point(Math.random() * bounds.w, Math.random() * bounds.h);
            quadtree.insert(p);
            const pointDiv = document.createElement('div');
            pointDiv.className = 'quadtree-point';
            Object.assign(pointDiv.style, { left: `${p.x}px`, top: `${p.y}px` });
            canvas.appendChild(pointDiv);
        }
        quadtree.draw(canvas);
        let queryRangeDiv = document.createElement('div');
        queryRangeDiv.className = 'query-range';
        canvas.appendChild(queryRangeDiv);
    }

    if(simBtn) simBtn.addEventListener('click', runSimulation);

    if(canvas) canvas.addEventListener('mousemove', (e) => {
        if (!quadtree) return;
        const rect = canvas.getBoundingClientRect();
        const x = e.clientX - rect.left, y = e.clientY - rect.top;
        const rangeSize = 80;
        const range = new Rectangle(x - rangeSize / 2, y - rangeSize / 2, rangeSize, rangeSize);
        const queryRangeDiv = canvas.querySelector('.query-range');
        if (queryRangeDiv) {
            Object.assign(queryRangeDiv.style, {
                left: `${range.x}px`,
                top: `${range.y}px`,
                width: `${range.w}px`,
                height: `${range.h}px`
            });
        }

        const pointsInRange = quadtree.query(range);
        canvas.querySelectorAll('.quadtree-point').forEach(p => p.classList.remove('point-in-range'));

        if (pointsInRange.length > 0) {
            const allPointDivs = Array.from(canvas.querySelectorAll('.quadtree-point'));
            pointsInRange.forEach(p => {
               for(const div of allPointDivs) {
                   if (Math.abs(parseFloat(div.style.left) - p.x) < 0.1 && Math.abs(parseFloat(div.style.top) - p.y) < 0.1) {
                       div.classList.add('point-in-range');
                       break;
                   }
               }
            });
        }
    });

    function initializePerfChart(){
        const canvasElement = document.getElementById('performanceChart');
        if (!canvasElement) return;
        // Check if a chart instance already exists and destroy it.
        let existingChart = Chart.getChart(canvasElement);
        if (existingChart) {
            existingChart.destroy();
        }

        const ctx = canvasElement.getContext('2d');
        new Chart(ctx, {
            type: 'bar',
            data: {
                labels: ['Packet Processing', 'DB Query', 'Pathfinding', 'State Sync'],
                datasets: [{
                    label: '기본 구현 (Mutex Lock)',
                    data: [120, 200, 350, 90],
                    backgroundColor: 'rgba(191, 97, 106, 0.7)',
                    borderColor: '#BF616A',
                    borderWidth: 1
                }, {
                    label: '최적화 구현 (Lock-Free)',
                    data: [40, 180, 250, 50],
                    backgroundColor: 'rgba(163, 190, 140, 0.7)',
                    borderColor: '#A3BE8C',
                    borderWidth: 1
                }]
            },
            options: {
                maintainAspectRatio: false,
                scales: {
                    y: {
                        beginAtZero: true,
                        grid: { color: '#4C566A' },
                        ticks: {
                            color: '#D8DEE9',
                            callback: (v) => v + 'μs'
                        }
                    },
                    x: {
                        grid: { display: false },
                        ticks: { color: '#D8DEE9' }
                    }
                },
                plugins: {
                    legend: {
                        labels: { color: '#D8DEE9' }
                    },
                    title: {
                        display: true,
                        text: '최적화 기법에 따른 예상 연산 시간 비교 (마이크로초)',
                        color: '#ECEFF4',
                        font: { size: 16 }
                    }
                }
            }
        });
    }
});